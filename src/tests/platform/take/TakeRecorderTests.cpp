#include <catch2/catch_test_macros.hpp>

#include "platform/take/TakeRecorder.h"

#include "application/testcontrol/SyntheticTake.h"

#include <cmath>
#include <deque>
#include <memory>
#include <vector>

using namespace take;
using transport::ClockState;
using transport::Frame;

namespace
{
constexpr score::Tick quarter = score::ticksPerQuarterNote;
constexpr int blockFrames = 512;
constexpr Frame bar = 4 * 22050; // 4/4 at 120 bpm, 44.1 kHz

[[nodiscard]] std::shared_ptr<const score::Score> scoreOf(int measures)
{
    auto result = std::make_shared<score::Score>();
    result->metadata.workTitle = "A Take Test";
    result->tempoMap = score::TempoMap::build({{0, 120.0}});
    score::Part part;
    for (int index = 0; index < measures; ++index)
    {
        score::Measure measure;
        measure.index = static_cast<std::size_t>(index);
        measure.start = static_cast<score::Tick>(index) * 4 * quarter;
        measure.nominalDuration = 4 * quarter;
        if (index == 0)
        {
            measure.attributes.time = score::TimeSignature{4, 4};
        }
        part.measures.push_back(std::move(measure));
    }
    result->parts.push_back(std::move(part));
    return result;
}

// A clock, a recorder and pretend sources, driven block by block.
struct RecorderUnderTest
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    score::CurrentScore currentScore;
    std::unique_ptr<transport::PracticeClock> clock;
    std::unique_ptr<TakeRecorder> recorder;

    bool microphoneOpen = true;
    bool microphoneMuted = false;
    bool keyboardOpen = false;
    std::deque<midi::MidiEvent> keys;
    int blocksDriven = 0;
    std::vector<float> input = std::vector<float>(blockFrames, 0.5f);

    explicit RecorderUnderTest(int measures = 2)
    {
        currentScore.publish(scoreOf(measures));
        clock = std::make_unique<transport::PracticeClock>([](RealtimeRenderer*) {}, currentScore);
        clock->setCountInBars(1);

        TakeSources sources;
        sources.microphoneOpen = [this] { return microphoneOpen; };
        sources.microphoneMuted = [this] { return microphoneMuted; };
        sources.keyboardOpen = [this] { return keyboardOpen; };
        sources.readKeyboard = [this](midi::MidiEvent* destination, std::size_t maximum)
        {
            std::size_t count = 0;
            while (count < maximum && !keys.empty())
            {
                destination[count++] = keys.front();
                keys.pop_front();
            }
            return count;
        };
        sources.latencies = [] { return std::pair<int, int>{256, 512}; };
        sources.referencePitch = [] { return 442.0; };
        recorder = std::make_unique<TakeRecorder>(*clock, currentScore, std::move(sources));

        // The clock's table has to reach the renderer before it has a score.
        block();
        tick();
    }

    // When the block about to be driven begins, on the high-resolution clock.
    [[nodiscard]] double blockTimeMilliseconds() const
    {
        return 1000.0 + static_cast<double>(blocksDriven) * blockFrames / 44.1;
    }

    void block()
    {
        RenderBlock render;
        render.input = input.data();
        render.frames = blockFrames;
        render.sampleRate = 44100.0;
        render.timeMilliseconds = blockTimeMilliseconds();
        clock->renderer().render(render);
        ++blocksDriven;
    }

    // What the timers would do: the clock notifies, the recorder drains.
    void tick()
    {
        clock->poll();
        recorder->poll();
    }

    void run(int blocks)
    {
        for (int index = 0; index < blocks; ++index)
        {
            block();
            if (index % 4 == 3)
            {
                tick();
            }
        }
        tick();
    }
};
} // namespace

TEST_CASE("a take cannot start without a score or a source, and says why", "[take][recorder]")
{
    RecorderUnderTest fixture;
    CHECK_FALSE(fixture.recorder->whyCannotStart().has_value());

    fixture.microphoneMuted = true;
    REQUIRE(fixture.recorder->whyCannotStart().has_value());
    CHECK(*fixture.recorder->whyCannotStart() == "the microphone is muted and no keyboard is open");
    CHECK_FALSE(fixture.recorder->start());

    fixture.keyboardOpen = true;
    CHECK_FALSE(fixture.recorder->whyCannotStart().has_value());

    fixture.currentScore.publish(nullptr);
    fixture.tick();
    REQUIRE(fixture.recorder->whyCannotStart().has_value());
    CHECK(*fixture.recorder->whyCannotStart() == "open a score first");
}

TEST_CASE("a take counts in, records to the end of the piece and is complete", "[take][recorder]")
{
    RecorderUnderTest fixture;
    REQUIRE(fixture.recorder->start());
    CHECK(fixture.recorder->isRecording());
    CHECK(fixture.clock->isLocked());

    fixture.run(10);
    REQUIRE(fixture.recorder->current() != nullptr);
    CHECK(fixture.recorder->current()->state() == TakeState::countingIn);

    // Through the count-in and both bars, and a little past the end.
    fixture.run(static_cast<int>((3 * bar) / blockFrames) + 8);

    CHECK_FALSE(fixture.recorder->isRecording());
    CHECK_FALSE(fixture.clock->isLocked());
    REQUIRE(fixture.recorder->takes().size() == 1);
    const Take& take = *fixture.recorder->takes().front();
    CHECK(take.state() == TakeState::completed);
    CHECK(take.id == 1);
    CHECK(take.scoreTitle == "A Take Test");
    CHECK(take.usedMicrophone);
    CHECK_FALSE(take.usedKeyboard);
    CHECK(take.inputLatencyFrames == 256);
    CHECK(take.referenceHz == 442.0);

    // From the count-in's first frame to the piece's last, with nothing missing.
    CHECK(take.audioStart == -bar);
    CHECK(take.countInFrames == bar);
    CHECK(take.audioEnd() == 2 * bar);
    CHECK(take.lastFrame == 2 * bar);
    CHECK(take.audio.at(0) == 0.5f);
    REQUIRE(take.table != nullptr);
    CHECK(take.tickAt(bar) == static_cast<double>(4 * quarter));
}

TEST_CASE("a take stopped part-way is kept as stopped early, with its bar", "[take][recorder]")
{
    RecorderUnderTest fixture(8);
    REQUIRE(fixture.recorder->start());
    fixture.run(static_cast<int>((bar * 3) / blockFrames));
    REQUIRE(fixture.recorder->current() != nullptr);
    CHECK(fixture.recorder->current()->state() == TakeState::performing);

    fixture.recorder->stop();
    fixture.run(4);

    REQUIRE(fixture.recorder->takes().size() == 1);
    const Take& take = *fixture.recorder->takes().front();
    CHECK(take.state() == TakeState::stoppedEarly);
    REQUIRE(take.stoppedInBar.has_value());
    CHECK(*take.stoppedInBar == 1);
    CHECK(fixture.clock->position().state == ClockState::stopped);
    CHECK_FALSE(fixture.clock->isLocked());
}

TEST_CASE("Stop on the transport ends a take the same way", "[take][recorder]")
{
    RecorderUnderTest fixture(8);
    REQUIRE(fixture.recorder->start());
    fixture.run(static_cast<int>((bar * 2) / blockFrames));
    fixture.clock->stop();
    fixture.run(2);

    REQUIRE(fixture.recorder->takes().size() == 1);
    CHECK(fixture.recorder->takes().front()->state() == TakeState::stoppedEarly);
}

TEST_CASE("a take stopped during the count-in, or cancelled, is not kept", "[take][recorder]")
{
    RecorderUnderTest fixture(8);
    REQUIRE(fixture.recorder->start());
    fixture.run(10);
    fixture.recorder->stop();
    CHECK(fixture.recorder->takes().empty());
    REQUIRE(fixture.recorder->lastEnded() != nullptr);
    CHECK(fixture.recorder->lastEnded()->state() == TakeState::cancelled);

    REQUIRE(fixture.recorder->start());
    fixture.run(static_cast<int>((bar * 2) / blockFrames));
    fixture.recorder->cancel();
    CHECK(fixture.recorder->takes().empty());
    CHECK(fixture.recorder->lastEnded()->id == 2);
}

TEST_CASE("nothing moves the clock under a take", "[take][recorder]")
{
    RecorderUnderTest fixture(8);
    fixture.clock->setLoopEnabled(true);
    fixture.tick();
    REQUIRE(fixture.recorder->start());
    CHECK_FALSE(fixture.clock->loopEnabled());

    fixture.clock->setScalePercent(50);
    fixture.clock->seekToBar(5);
    fixture.run(static_cast<int>((bar * 2) / blockFrames));
    CHECK(fixture.clock->scalePercent() == 100);
    REQUIRE(fixture.recorder->current() != nullptr);
    CHECK(fixture.recorder->current()->state() == TakeState::performing);
}

TEST_CASE("opening another score fails the take, keeping it", "[take][recorder]")
{
    RecorderUnderTest fixture(8);
    REQUIRE(fixture.recorder->start());
    fixture.run(static_cast<int>((bar * 2) / blockFrames));

    fixture.currentScore.publish(scoreOf(3));
    fixture.recorder->poll();

    REQUIRE(fixture.recorder->takes().size() == 1);
    const Take& take = *fixture.recorder->takes().front();
    CHECK(take.state() == TakeState::failed);
    CHECK(take.lifecycle.failureReason() == "another score was opened");
    CHECK_FALSE(take.audio.empty());
    CHECK_FALSE(fixture.clock->isLocked());
}

TEST_CASE("losing every source fails the take", "[take][recorder]")
{
    RecorderUnderTest fixture(8);
    REQUIRE(fixture.recorder->start());
    fixture.run(20);

    // Muting is not losing the microphone: it goes on, recording silence.
    fixture.microphoneMuted = true;
    fixture.run(4);
    CHECK(fixture.recorder->isRecording());

    fixture.microphoneOpen = false;
    fixture.recorder->poll();
    CHECK_FALSE(fixture.recorder->isRecording());
    REQUIRE(fixture.recorder->lastEnded() != nullptr);
    CHECK(fixture.recorder->lastEnded()->state() == TakeState::failed);
}

TEST_CASE("keyboard events are placed on the take's timeline", "[take][recorder]")
{
    RecorderUnderTest fixture(8);
    fixture.microphoneMuted = true;
    fixture.keyboardOpen = true;
    REQUIRE(fixture.recorder->start());

    // Into the music, then a key 10 ms after the next block begins.
    fixture.run(static_cast<int>((bar * 3 / 2) / blockFrames));
    const Frame blockStart = fixture.clock->renderer().snapshot().position;
    const double pressedAt = fixture.blockTimeMilliseconds() + 10.0;
    fixture.block();

    midi::MidiEvent key;
    key.kind = midi::MidiEventKind::noteOn;
    key.noteNumber = 60;
    key.velocity = 90;
    key.deviceTimestampSeconds = pressedAt / 1000.0;
    fixture.keys.push_back(key);
    fixture.tick();

    REQUIRE(fixture.recorder->current() != nullptr);
    const Take& take = *fixture.recorder->current();
    CHECK(take.usedKeyboard);
    CHECK_FALSE(take.usedMicrophone);
    CHECK(take.audio.empty());
    REQUIRE(take.keyboard.size() == 1);
    CHECK(take.keyboard.front().frame == blockStart + 441);
    CHECK(take.keyboard.front().event.noteNumber == 60);
}

TEST_CASE("a paused take resumes without a gap in its audio", "[take][recorder]")
{
    RecorderUnderTest fixture(8);
    REQUIRE(fixture.recorder->start());
    fixture.run(static_cast<int>((bar * 2) / blockFrames));
    fixture.clock->pause();
    fixture.run(10);
    REQUIRE(fixture.recorder->current() != nullptr);
    CHECK(fixture.recorder->current()->state() == TakeState::paused);

    // Playing again counts in again; that count-in is heard, not kept.
    fixture.clock->play();
    fixture.run(static_cast<int>((bar * 2) / blockFrames));
    REQUIRE(fixture.recorder->current() != nullptr);
    CHECK(fixture.recorder->current()->state() == TakeState::performing);

    fixture.recorder->stop();
    REQUIRE(fixture.recorder->takes().size() == 1);
    const Take& take = *fixture.recorder->takes().front();
    CHECK(take.state() == TakeState::stoppedEarly);
    // About a bar before the pause and a bar after it, as one run: the second
    // count-in is not in it.
    CHECK(take.audioEnd() > bar * 3 / 2);
    CHECK(take.countInFrames == bar);
}

namespace
{
// One bar of quarters per part, at 120 bpm: a voice above, a piano below.
[[nodiscard]] std::shared_ptr<const score::Score> voiceAndPiano()
{
    auto result = std::make_shared<score::Score>();
    result->tempoMap = score::TempoMap::build({{0, 120.0}});
    const std::vector<std::vector<int>> parts{{76, 77, 79, 81}, {60, 64, 67, 72}};
    for (const auto& pitches : parts)
    {
        score::Part part;
        score::Measure measure;
        measure.nominalDuration = 4 * quarter;
        measure.attributes.time = score::TimeSignature{4, 4};
        score::Voice voice;
        for (std::size_t beat = 0; beat < pitches.size(); ++beat)
        {
            score::ScoreEvent event;
            event.kind = score::EventKind::note;
            event.onset = static_cast<score::Tick>(beat) * quarter;
            event.duration = quarter;
            score::Pitch pitch;
            pitch.midiNoteNumber = pitches[beat];
            event.notes.push_back(score::Note{pitch, pitch, std::nullopt, std::nullopt});
            voice.events.push_back(event);
        }
        measure.voices.push_back(voice);
        part.measures.push_back(measure);
        result->parts.push_back(part);
    }
    return result;
}

// A completed keyboard take of `source`, pressing `keys` a beat apart.
[[nodiscard]] Take keyboardTakeOf(std::shared_ptr<const score::Score> source, std::vector<int> keys)
{
    Take result;
    auto table = std::make_shared<transport::ClockTable>();
    transport::buildTable(*table, *source, 100, 44100.0);
    result.score = std::move(source);
    result.table = table;
    result.usedKeyboard = true;
    for (std::size_t beat = 0; beat < keys.size(); ++beat)
    {
        KeyboardEvent key;
        key.frame = static_cast<Frame>(beat) * 22050;
        key.event.kind = midi::MidiEventKind::noteOn;
        key.event.noteNumber = keys[beat];
        key.event.velocity = 80;
        result.keyboard.push_back(key);
    }
    result.lifecycle.apply(TakeEvent::countInStarted);
    result.lifecycle.apply(TakeEvent::performanceStarted);
    result.lifecycle.apply(TakeEvent::reachedEnd);
    return result;
}

struct CountingListener final : TakeListener
{
    int changes = 0;
    void takesChanged() override
    {
        ++changes;
    }
};
} // namespace

TEST_CASE("a kept take is reviewed against the part it matches best", "[take][recorder][review]")
{
    RecorderUnderTest fixture;
    CountingListener listener;
    fixture.recorder->addListener(&listener);
    CHECK(fixture.recorder->review() == nullptr);

    // The pianist played the piano part, one note wrong.
    const std::uint64_t id =
        fixture.recorder->keep(keyboardTakeOf(voiceAndPiano(), {60, 64, 69, 72}));
    CHECK(id == 1);
    REQUIRE(fixture.recorder->takes().size() == 1);
    CHECK(fixture.recorder->lastEnded()->id == id);

    fixture.recorder->finishJudging();
    CHECK_FALSE(fixture.recorder->isJudging());
    const TakeReview* review = fixture.recorder->review();
    REQUIRE(review != nullptr);
    CHECK(review->take->id == id);
    CHECK(review->part == 1);
    CHECK(review->report.summary.correct == 3);
    CHECK(review->report.summary.wrong == 1);
    CHECK(listener.changes >= 2);

    // The same take against the voice: every note wrong.
    CHECK(fixture.recorder->reviewTake(id, 0));
    fixture.recorder->finishJudging();
    REQUIRE(fixture.recorder->review() != nullptr);
    CHECK(fixture.recorder->review()->part == 0);
    CHECK(fixture.recorder->review()->report.summary.wrong == 4);

    // Neither a take that was not kept nor a part the score lacks.
    CHECK_FALSE(fixture.recorder->reviewTake(99));
    CHECK_FALSE(fixture.recorder->reviewTake(id, 2));
    fixture.recorder->removeListener(&listener);
}

TEST_CASE("a take that has not ended, or was cancelled, is not kept", "[take][recorder][review]")
{
    RecorderUnderTest fixture;
    CHECK(fixture.recorder->keep(Take{}) == 0);

    Take cancelled = keyboardTakeOf(voiceAndPiano(), {60});
    cancelled.lifecycle = TakeLifecycle{};
    cancelled.lifecycle.apply(TakeEvent::countInStarted);
    cancelled.lifecycle.apply(TakeEvent::cancelled);
    CHECK(fixture.recorder->keep(std::move(cancelled)) == 0);
    CHECK(fixture.recorder->takes().empty());
}

TEST_CASE("a take recorded here is reviewed when it ends", "[take][recorder][review]")
{
    RecorderUnderTest fixture;
    REQUIRE(fixture.recorder->start());
    fixture.run(static_cast<int>((3 * bar) / blockFrames) + 8);
    REQUIRE(fixture.recorder->takes().size() == 1);

    // Judged on the worker, taken by a poll.
    while (fixture.recorder->isJudging())
    {
        fixture.recorder->poll();
    }
    const TakeReview* review = fixture.recorder->review();
    REQUIRE(review != nullptr);
    CHECK(review->take == fixture.recorder->takes().front());

    // A newer take's review replaces it; the older take's marks go at once.
    const std::uint64_t id = fixture.recorder->keep(keyboardTakeOf(voiceAndPiano(), {60}));
    CHECK(fixture.recorder->review() == nullptr);
    fixture.recorder->finishJudging();
    REQUIRE(fixture.recorder->review() != nullptr);
    CHECK(fixture.recorder->review()->take->id == id);
}

TEST_CASE("forgetting takes leaves nothing to review", "[take][recorder][review]")
{
    RecorderUnderTest fixture;
    static_cast<void>(fixture.recorder->keep(keyboardTakeOf(voiceAndPiano(), {60, 64})));
    fixture.recorder->finishJudging();
    REQUIRE(fixture.recorder->review() != nullptr);

    // Forgotten while a second judgement runs: it is dropped when it finishes.
    static_cast<void>(fixture.recorder->keep(keyboardTakeOf(voiceAndPiano(), {60})));
    fixture.recorder->forgetTakes();
    CHECK(fixture.recorder->takes().empty());
    CHECK(fixture.recorder->lastEnded() == nullptr);
    CHECK(fixture.recorder->review() == nullptr);
    CHECK_FALSE(fixture.recorder->isJudging());
    fixture.recorder->finishJudging();
    CHECK(fixture.recorder->review() == nullptr);

    // Ids keep counting, so a take is never confused with a forgotten one.
    CHECK(fixture.recorder->keep(keyboardTakeOf(voiceAndPiano(), {60})) == 3);
}

TEST_CASE(
    "a new tolerance judges the take under review again",
    "[take][recorder][review][intonation]")
{
    RecorderUnderTest fixture;

    // Every note sung 15 cents sharp of A440.
    take::Take sung = testcontrol::syntheticSungTake(
        voiceAndPiano(), 0, 1, 44100.0, 440.0 * std::pow(2.0, 15.0 / 1200.0));
    sung.referenceHz = 440.0;
    const std::uint64_t id = fixture.recorder->keep(std::move(sung));
    REQUIRE(id != 0);
    fixture.recorder->finishJudging();
    REQUIRE(fixture.recorder->review() != nullptr);
    const std::size_t looseSharp = fixture.recorder->review()->report.summary.sharp;
    CHECK(fixture.recorder->review()->report.summary.inTune > 0);

    evaluation::IntonationSettings strict;
    strict.toleranceCents = 10.0;
    fixture.recorder->setIntonation(strict);
    CHECK(fixture.recorder->intonationSettings().toleranceCents == 10.0);
    fixture.recorder->finishJudging();
    REQUIRE(fixture.recorder->review() != nullptr);
    CHECK(fixture.recorder->review()->take->id == id);
    CHECK(fixture.recorder->review()->report.summary.inTune == 0);
    CHECK(fixture.recorder->review()->report.summary.sharp > looseSharp);

    // The same settings again judge nothing.
    fixture.recorder->setIntonation(strict);
    CHECK_FALSE(fixture.recorder->isJudging());
}
