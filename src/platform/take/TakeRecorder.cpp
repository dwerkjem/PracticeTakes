#include "TakeRecorder.h"

#include "../evaluation/TakeReview.h"
#include "TakeAssembler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <new>

namespace take
{
namespace
{
constexpr std::size_t keyBufferSize = 256;

// Judging runs off the message thread on a take nothing changes any more: a
// kept take, its score and its table are all immutable.
[[nodiscard]] TakeReview judge(
    std::shared_ptr<const Take> take,
    std::optional<std::size_t> part,
    evaluation::TimingSettings timing,
    evaluation::IntonationSettings intonation)
{
    TakeReview review;
    review.take = std::move(take);
    try
    {
        if (part.has_value())
        {
            review.part = *part;
            review.report = evaluation::judgeNotes(*review.take, *part, timing, intonation);
        }
        else
        {
            evaluation::ReviewedPart best =
                evaluation::bestPartFor(*review.take, timing, intonation);
            review.part = best.part;
            review.report = std::move(best.report);
        }
    }
    catch (const std::bad_alloc&)
    {
        // A take too long to judge is reviewed with nothing judged rather than
        // ending the application from a timer callback.
        review.report = {};
    }
    return review;
}

[[nodiscard]] bool isReady(const std::future<TakeReview>& result)
{
    return result.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}
} // namespace

TakeRecorder::TakeRecorder(
    transport::PracticeClock& practiceClock,
    const score::CurrentScore& currentScore,
    TakeSources takeSources)
    : clock(practiceClock), scoreHolder(currentScore), sources(std::move(takeSources)),
      keyBuffer(keyBufferSize)
{
    clock.addListener(this);
    startTimerHz(drainRateHz);
}

TakeRecorder::~TakeRecorder()
{
    stopTimer();
    if (live != nullptr)
    {
        finish(TakeEvent::stopped);
    }
    clock.removeListener(this);
}

bool TakeRecorder::microphoneUsable() const
{
    return sources.microphoneOpen && sources.microphoneOpen() &&
           !(sources.microphoneMuted && sources.microphoneMuted());
}

bool TakeRecorder::keyboardUsable() const
{
    return sources.keyboardOpen && sources.keyboardOpen();
}

std::optional<std::string> TakeRecorder::whyCannotStart() const
{
    if (live != nullptr)
    {
        return std::string("a take is already being recorded");
    }
    if (scoreHolder.score() == nullptr || !clock.hasScore())
    {
        return std::string("open a score first");
    }
    if (!microphoneUsable() && !keyboardUsable())
    {
        const bool muted = sources.microphoneOpen && sources.microphoneOpen() &&
                           sources.microphoneMuted && sources.microphoneMuted();
        return std::string(
            muted ? "the microphone is muted and no keyboard is open"
                  : "there is no microphone or keyboard to record from");
    }
    return std::nullopt;
}

bool TakeRecorder::start()
{
    if (whyCannotStart().has_value())
    {
        return false;
    }

    const transport::ClockPosition position = clock.position();
    const transport::ClockTiming timing = clock.timing();

    auto take = std::make_unique<Take>();
    take->id = ++nextId;
    take->score = scoreHolder.score();
    take->scoreTitle = take->score->metadata.workTitle.empty() ? take->score->metadata.movementTitle
                                                               : take->score->metadata.workTitle;
    take->startTick = static_cast<score::Tick>(std::llround(std::max(0.0, position.tick)));
    take->scalePercent = clock.scalePercent();
    take->sampleRate = timing.sampleRate > 0.0 ? timing.sampleRate : 44100.0;
    if (sources.latencies)
    {
        const auto [input, output] = sources.latencies();
        take->inputLatencyFrames = input;
        take->outputLatencyFrames = output;
    }
    if (sources.referencePitch)
    {
        take->referenceHz = sources.referencePitch();
    }
    take->usedMicrophone = microphoneUsable();
    take->usedKeyboard = keyboardUsable();

    // A take plays through once: no loop to wrap, and nothing moved under it
    // (design.md decision 4). Paused first if it was running, so the count-in
    // starts from where it is.
    if (position.state == transport::ClockState::playing)
    {
        clock.pause();
    }
    clock.setLoopEnabled(false);
    take->table = clock.currentTable();

    live = std::move(take);
    sawRunning = false;
    lastBar = position.bar;

    if (live->usedKeyboard && sources.beginKeyboard)
    {
        sources.beginKeyboard();
    }
    if (live->usedMicrophone)
    {
        clock.renderer().armRecording();
    }

    clock.lock();
    clock.play();
    notify();
    return true;
}

void TakeRecorder::stop()
{
    if (live == nullptr)
    {
        return;
    }
    finish(TakeEvent::stopped);
    clock.stop();
}

void TakeRecorder::cancel()
{
    if (live == nullptr)
    {
        return;
    }
    finish(TakeEvent::cancelled);
    clock.stop();
}

bool TakeRecorder::isRecording() const noexcept
{
    return live != nullptr;
}

const Take* TakeRecorder::current() const noexcept
{
    return live.get();
}

const std::vector<std::shared_ptr<const Take>>& TakeRecorder::takes() const noexcept
{
    return kept;
}

const Take* TakeRecorder::lastEnded() const noexcept
{
    return ended.get();
}

void TakeRecorder::addListener(TakeListener* listener)
{
    if (listener != nullptr &&
        std::find(listeners.begin(), listeners.end(), listener) == listeners.end())
    {
        listeners.push_back(listener);
    }
}

void TakeRecorder::removeListener(TakeListener* listener)
{
    listeners.erase(std::remove(listeners.begin(), listeners.end(), listener), listeners.end());
}

void TakeRecorder::notify()
{
    const auto toNotify = listeners;
    for (TakeListener* listener : toNotify)
    {
        if (std::find(listeners.begin(), listeners.end(), listener) != listeners.end())
        {
            listener->takesChanged();
        }
    }
}

void TakeRecorder::timerCallback()
{
    poll();
}

std::uint64_t TakeRecorder::keep(Take take)
{
    if (!isKept(take.state()))
    {
        return 0;
    }
    take.id = ++nextId;
    const std::uint64_t id = take.id;
    auto done = std::make_shared<const Take>(std::move(take));
    ended = done;
    retain(std::move(done));
    static_cast<void>(reviewTake(id));
    return id;
}

bool TakeRecorder::reviewTake(std::uint64_t id, std::optional<std::size_t> part)
{
    const auto found = std::find_if(
        kept.begin(), kept.end(),
        [id](const std::shared_ptr<const Take>& candidate) { return candidate->id == id; });
    if (found == kept.end())
    {
        return false;
    }
    const std::shared_ptr<const Take>& take = *found;
    if (part.has_value() && (take->score == nullptr || *part >= take->score->parts.size()))
    {
        return false;
    }

    // Another take's marks would be wrong for this one; the same take's, while
    // its part changes, are only out of date.
    if (reviewed.has_value() && reviewed->take != take)
    {
        reviewed.reset();
    }
    requested = Requested{id, part};
    judging.push_back(Judging{
        ++nextRequest, std::async(std::launch::async, judge, take, part, timing, intonation)});
    notify();
    return true;
}

void TakeRecorder::setIntonation(const evaluation::IntonationSettings& settings)
{
    if (settings == intonation)
    {
        return;
    }
    intonation = settings;
    rejudge();
}

void TakeRecorder::rejudge()
{
    // Judged again against the part it is already reviewed against, or the
    // part asked for.
    if (requested.has_value())
    {
        const std::optional<std::size_t> part =
            reviewed.has_value() && reviewed->take->id == requested->id
                ? std::optional{reviewed->part}
                : requested->part;
        static_cast<void>(reviewTake(requested->id, part));
    }
}

const evaluation::IntonationSettings& TakeRecorder::intonationSettings() const noexcept
{
    return intonation;
}

void TakeRecorder::setMinimumLevelDb(double level)
{
    if (level == timing.onsets.minimumLevelDb)
    {
        return;
    }
    timing.onsets.minimumLevelDb = level;
    rejudge();
}

double TakeRecorder::minimumLevelDb() const noexcept
{
    return timing.onsets.minimumLevelDb;
}

const TakeReview* TakeRecorder::review() const noexcept
{
    return reviewed.has_value() ? &*reviewed : nullptr;
}

bool TakeRecorder::isJudging() const noexcept
{
    return !judging.empty() && judging.back().request == nextRequest;
}

void TakeRecorder::forgetTakes()
{
    kept.clear();
    ended.reset();
    reviewed.reset();
    requested.reset();

    // Judging still running finishes and is dropped: its request is no longer
    // the newest.
    ++nextRequest;
    notify();
}

void TakeRecorder::finishJudging()
{
    adoptJudgement(true);
}

void TakeRecorder::adoptJudgement(bool wait)
{
    bool adopted = false;
    if (isJudging())
    {
        Judging& newest = judging.back();
        if (wait)
        {
            newest.result.wait();
        }
        if (isReady(newest.result))
        {
            reviewed = newest.result.get();
            judging.pop_back();
            adopted = true;
        }
    }

    // Older requests are dropped once they finish: destroying a future that
    // std::async made waits for it, which the message thread must not do.
    judging.erase(
        std::remove_if(
            judging.begin(), judging.end(), [this](const Judging& entry)
            { return entry.request != nextRequest && isReady(entry.result); }),
        judging.end());

    if (adopted)
    {
        notify();
    }
}

void TakeRecorder::retain(std::shared_ptr<const Take> take)
{
    kept.push_back(std::move(take));
    if (kept.size() > maximumTakes)
    {
        kept.erase(
            kept.begin(), kept.begin() + static_cast<std::ptrdiff_t>(kept.size() - maximumTakes));
    }
}

void TakeRecorder::poll()
{
    adoptJudgement(false);
    if (live == nullptr)
    {
        return;
    }

    if (!drain())
    {
        finish(TakeEvent::failed, "the clock jumped while recording");
        clock.stop();
        return;
    }

    if (scoreHolder.score() != live->score)
    {
        finish(TakeEvent::failed, "another score was opened");
        clock.stop();
        return;
    }

    if (clock.renderer().recordingOverflows() > 0)
    {
        finish(TakeEvent::failed, "the recording fell behind");
        clock.stop();
        return;
    }

    // Every source it was recording from has gone: an unplugged microphone and
    // keyboard. Muting is not losing a source -- it records silence.
    const bool microphoneGone =
        live->usedMicrophone && !(sources.microphoneOpen && sources.microphoneOpen());
    const bool keyboardGone = live->usedKeyboard && !keyboardUsable();
    if ((microphoneGone || !live->usedMicrophone) && (keyboardGone || !live->usedKeyboard))
    {
        finish(TakeEvent::failed, "the microphone and keyboard were disconnected");
        clock.stop();
    }
}

void TakeRecorder::clockChanged(const transport::ClockUpdate& update)
{
    if (live == nullptr)
    {
        return;
    }
    if (!drain())
    {
        finish(TakeEvent::failed, "the clock jumped while recording");
        clock.stop();
        return;
    }
    follow(update);
}

void TakeRecorder::follow(const transport::ClockUpdate& update)
{
    const transport::ClockPosition& position = update.position;
    const bool running = position.state == transport::ClockState::playing;

    if (running)
    {
        sawRunning = true;
        lastBar = position.bar;
        if (!position.countingIn)
        {
            live->lastFrame = std::max(live->lastFrame, position.frame);
        }
    }

    if (update.reachedEnd && sawRunning)
    {
        // The end of the piece: the last frame is the piece's end.
        if (live->table != nullptr)
        {
            live->lastFrame = std::max(live->lastFrame, live->table->endFrame);
        }
        finish(live->lifecycle.hasPerformed() ? TakeEvent::reachedEnd : TakeEvent::stopped);
        return;
    }

    if (!sawRunning)
    {
        return;
    }

    bool changed = false;
    switch (position.state)
    {
    case transport::ClockState::playing:
        changed = live->lifecycle.apply(
            position.countingIn ? TakeEvent::countInStarted : TakeEvent::performanceStarted);
        break;
    case transport::ClockState::paused:
        changed = live->lifecycle.apply(TakeEvent::paused);
        break;
    case transport::ClockState::stopped:
        // Stopped from the transport rather than by Record.
        finish(TakeEvent::stopped);
        return;
    }

    if (changed)
    {
        notify();
    }
}

bool TakeRecorder::drain()
{
    transport::ClockRenderer& renderer = clock.renderer();
    transport::RecordedBlock block;
    bool continuous = true;
    while (renderer.popRecordedBlock(block))
    {
        scratch.resize(block.frames);
        const std::size_t read = renderer.readRecording(scratch.data(), scratch.size());
        if (live == nullptr || read != block.frames || !continuous)
        {
            continue;
        }

        // A count-in after a pause counts in again over frames already
        // recorded: it is heard, not kept.
        if (block.countingIn && live->lifecycle.hasPerformed())
        {
            continue;
        }

        // Everything after a gap is drained but not kept: the take ends at
        // the gap.
        continuous = appendBlock(*live, block, scratch.data()) == AssemblyResult::appended;
    }

    if (live == nullptr || !live->usedKeyboard || !sources.readKeyboard)
    {
        return continuous;
    }

    const transport::ClockTiming timing = clock.timing();
    for (;;)
    {
        const std::size_t count = sources.readKeyboard(keyBuffer.data(), keyBuffer.size());
        for (std::size_t index = 0; index < count; ++index)
        {
            const midi::MidiEvent& event = keyBuffer.at(index);
            const double seconds = event.deviceTimestampSeconds > 0.0
                                       ? event.deviceTimestampSeconds
                                       : event.receivedTimestampSeconds;
            live->keyboard.push_back(KeyboardEvent{frameAtSeconds(timing, seconds), event});
        }
        if (count < keyBuffer.size())
        {
            break;
        }
    }
    return continuous;
}

void TakeRecorder::finish(TakeEvent event, std::string reason)
{
    if (live == nullptr)
    {
        return;
    }

    // What was recorded up to now is the take's; nothing after. A gap found
    // here ends the take where it was anyway, so the result is not needed.
    clock.renderer().disarmRecording();
    static_cast<void>(drain());

    if (live->usedKeyboard && sources.endKeyboard)
    {
        sources.endKeyboard();
    }

    if (event == TakeEvent::stopped && live->lifecycle.hasPerformed())
    {
        live->stoppedInBar = lastBar;
    }
    live->lifecycle.apply(event, std::move(reason));
    clock.unlock();

    std::shared_ptr<const Take> done(live.release());
    ended = done;
    if (isKept(done->state()))
    {
        const std::uint64_t id = done->id;
        retain(std::move(done));
        static_cast<void>(reviewTake(id));
    }
    notify();
}
} // namespace take
