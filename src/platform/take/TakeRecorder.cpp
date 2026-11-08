#include "TakeRecorder.h"

#include "TakeAssembler.h"

#include <algorithm>
#include <cmath>

namespace take
{
namespace
{
constexpr std::size_t keyBufferSize = 256;
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

void TakeRecorder::poll()
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
        kept.push_back(std::move(done));
        if (kept.size() > maximumTakes)
        {
            kept.erase(
                kept.begin(),
                kept.begin() + static_cast<std::ptrdiff_t>(kept.size() - maximumTakes));
        }
    }
    notify();
}
} // namespace take
