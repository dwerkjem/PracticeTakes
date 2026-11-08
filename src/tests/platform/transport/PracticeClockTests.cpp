#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "platform/score/CurrentScore.h"
#include "platform/transport/PracticeClock.h"

#include <array>
#include <memory>
#include <vector>

using Catch::Approx;
using namespace transport;

namespace
{
constexpr Tick quarter = score::ticksPerQuarterNote;

[[nodiscard]] std::shared_ptr<const score::Score>
scoreOf(int measures, score::TimeSignature time, double bpm)
{
    auto result = std::make_shared<score::Score>();
    result->tempoMap = score::TempoMap::build({{0, bpm}});
    score::Part part;
    Tick start = 0;
    for (int index = 0; index < measures; ++index)
    {
        score::Measure measure;
        measure.index = static_cast<std::size_t>(index);
        measure.start = start;
        if (index == 0)
        {
            measure.attributes.time = time;
        }
        measure.nominalDuration = score::nominalTicks(time);
        start += measure.nominalDuration;
        part.measures.push_back(std::move(measure));
    }
    result->parts.push_back(std::move(part));
    return result;
}

// A clock with no device: blocks come from the test, as the audio callback
// would deliver them, and notifications from calling poll().
struct ClockUnderTest
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    score::CurrentScore currentScore;
    RealtimeRenderer* attached = nullptr;
    int attachCalls = 0;
    std::unique_ptr<PracticeClock> clock;

    explicit ClockUnderTest(std::shared_ptr<const score::Score> source = scoreOf(16, {4, 4}, 120.0))
    {
        currentScore.publish(std::move(source));
        clock = std::make_unique<PracticeClock>(
            [this](RealtimeRenderer* renderer)
            {
                attached = renderer;
                ++attachCalls;
            },
            currentScore);
    }

    void block(int frames = 512)
    {
        std::vector<float> left(static_cast<std::size_t>(frames), 0.0f);
        std::array<float*, 1> channels{left.data()};
        RenderBlock render;
        render.outputs = channels.data();
        render.outputChannels = 1;
        render.frames = frames;
        render.sampleRate = 44100.0;
        render.timeMilliseconds = 1000.0 + static_cast<double>(blocksDriven) * 512.0 / 44.1;
        clock->renderer().render(render);
        ++blocksDriven;
    }

    void blocks(int count)
    {
        for (int index = 0; index < count; ++index)
        {
            block();
        }
    }

    int blocksDriven = 0;
};

struct Recorder final : ClockListener
{
    std::vector<ClockUpdate> updates;

    void clockChanged(const ClockUpdate& update) override
    {
        updates.push_back(update);
    }

    [[nodiscard]] int seeks() const
    {
        int total = 0;
        for (const auto& update : updates)
        {
            total += update.seeks;
        }
        return total;
    }

    [[nodiscard]] int wraps() const
    {
        int total = 0;
        for (const auto& update : updates)
        {
            total += update.wraps;
        }
        return total;
    }
};
} // namespace

TEST_CASE("the clock hands its renderer to the audio path and takes it back", "[transport][clock]")
{
    RealtimeRenderer* attached = nullptr;
    {
        ClockUnderTest fixture;
        CHECK(fixture.attached == &fixture.clock->renderer());
        attached = fixture.attached;
        fixture.clock.reset();
        CHECK(fixture.attached == nullptr);
        CHECK(fixture.attachCalls == 2);
    }
    CHECK(attached != nullptr);
}

TEST_CASE("play, pause and stop move the clock between its states", "[transport][clock]")
{
    ClockUnderTest fixture;
    auto& clock = *fixture.clock;
    clock.setCountInBars(0);

    fixture.block();
    clock.poll();
    CHECK(clock.position().state == ClockState::stopped);
    CHECK(clock.position().hasScore);

    clock.play();
    fixture.blocks(4);
    CHECK(clock.position().state == ClockState::playing);
    CHECK(clock.position().frame == 4 * 512);

    clock.pause();
    fixture.blocks(4);
    CHECK(clock.position().state == ClockState::paused);
    CHECK(clock.position().frame == 4 * 512);

    // Pause holds the position: play resumes from the frame it paused at.
    clock.togglePlay();
    fixture.block();
    CHECK(clock.position().frame == 5 * 512);

    clock.stop();
    fixture.block();
    CHECK(clock.position().state == ClockState::stopped);
    CHECK(clock.position().frame == 0);
}

TEST_CASE("two readers in one tick see the same position", "[transport][clock]")
{
    ClockUnderTest fixture;
    auto& clock = *fixture.clock;
    clock.setCountInBars(0);
    Recorder first;
    Recorder second;
    clock.addListener(&first);
    clock.addListener(&second);

    clock.play();
    fixture.blocks(30);
    clock.poll();

    REQUIRE_FALSE(first.updates.empty());
    REQUIRE(first.updates.size() == second.updates.size());
    const auto& a = first.updates.back().position;
    const auto& b = second.updates.back().position;
    CHECK(a.frame == b.frame);
    CHECK(a.tick == b.tick);
    CHECK(a.seconds == b.seconds);

    // 30 blocks of 512 at 120 bpm 44.1 kHz: 15,360 frames, 0.348 s, so
    // 0.348 s / 0.5 s per quarter of ticks.
    CHECK(a.seconds == Approx(15360.0 / 44100.0));
    CHECK(a.tick == Approx(15360.0 / 22050.0 * static_cast<double>(quarter)));

    clock.removeListener(&first);
    clock.removeListener(&second);
}

TEST_CASE("a seek to bar 9 is reported once", "[transport][clock]")
{
    ClockUnderTest fixture;
    auto& clock = *fixture.clock;
    Recorder recorder;
    clock.addListener(&recorder);

    clock.seekToBar(8);
    fixture.block();
    clock.poll();
    fixture.blocks(3);
    clock.poll();
    clock.poll();

    CHECK(recorder.seeks() == 1);
    REQUIRE_FALSE(recorder.updates.empty());
    const auto position = clock.position();
    CHECK(position.bar == 8);
    CHECK(position.beat == 0);
    CHECK(position.tick == Approx(static_cast<double>(8 * 4 * quarter)));

    clock.removeListener(&recorder);
}

TEST_CASE("a loop wrap between notifications is reported exactly once", "[transport][clock]")
{
    ClockUnderTest fixture;
    auto& clock = *fixture.clock;
    clock.setCountInBars(0);
    Recorder recorder;
    clock.addListener(&recorder);

    // One bar: 88,200 frames.
    clock.setLoopRegion(0, 4 * quarter);
    clock.setLoopEnabled(true);
    fixture.block();
    clock.poll();
    clock.play();

    // Just past one wrap, then a notification, then more blocks with no wrap.
    fixture.blocks(88200 / 512 + 1);
    clock.poll();
    fixture.blocks(10);
    clock.poll();

    CHECK(recorder.wraps() == 1);
    CHECK(recorder.updates.back().wraps == 0);
    CHECK(clock.position().frame < 88200);

    clock.removeListener(&recorder);
}

TEST_CASE("looping with no region set plays the whole score round", "[transport][clock]")
{
    ClockUnderTest fixture(scoreOf(1, {4, 4}, 120.0));
    auto& clock = *fixture.clock;
    clock.setCountInBars(0);
    clock.setLoopEnabled(true);
    fixture.block();
    clock.poll();
    clock.play();
    fixture.blocks(200);

    const auto events = clock.renderer().events();
    CHECK(events.wraps == 1);
    CHECK(events.ends == 0);
    CHECK(clock.position().state == ClockState::playing);
}

TEST_CASE("the count-in is reported, then the take begins", "[transport][clock]")
{
    ClockUnderTest fixture(scoreOf(4, {3, 4}, 60.0));
    auto& clock = *fixture.clock;
    clock.setCountInBars(1);
    Recorder recorder;
    clock.addListener(&recorder);

    clock.play();
    fixture.block();
    clock.poll();

    REQUIRE_FALSE(recorder.updates.empty());
    auto position = recorder.updates.back().position;
    CHECK(position.countingIn);
    CHECK(position.tick < 0.0);
    CHECK(position.bar == -1);
    CHECK(position.beat == 0);

    // Into the second count-in beat.
    fixture.blocks(44100 / 512 + 1);
    clock.poll();
    position = recorder.updates.back().position;
    CHECK(position.countingIn);
    CHECK(position.beat == 1);

    fixture.blocks(2 * 44100 / 512 + 2);
    clock.poll();
    position = recorder.updates.back().position;
    CHECK_FALSE(position.countingIn);
    CHECK(position.tick >= 0.0);
    CHECK(position.bar == 0);

    clock.removeListener(&recorder);
}

TEST_CASE("the end of the piece is reported once", "[transport][clock]")
{
    ClockUnderTest fixture(scoreOf(1, {4, 4}, 120.0));
    auto& clock = *fixture.clock;
    clock.setCountInBars(0);
    Recorder recorder;
    clock.addListener(&recorder);

    fixture.block();
    clock.play();
    fixture.blocks(200);
    clock.poll();
    clock.poll();

    int ends = 0;
    for (const auto& update : recorder.updates)
    {
        ends += update.reachedEnd ? 1 : 0;
    }
    CHECK(ends == 1);
    CHECK(clock.position().state == ClockState::stopped);

    clock.removeListener(&recorder);
}

TEST_CASE("a tempo-scale change is republished and reported", "[transport][clock]")
{
    ClockUnderTest fixture;
    auto& clock = *fixture.clock;
    clock.setCountInBars(0);
    clock.play();
    fixture.blocks(20);

    const double tickBefore = clock.position().tick;
    clock.setScalePercent(50);
    CHECK(clock.scalePercent() == 50);
    fixture.block(1);

    const auto position = clock.position();
    CHECK(position.scalePercent == 50);
    CHECK(position.tick == Approx(tickBefore).margin(2.0));

    // The lowest the scale goes is 5%.
    clock.setScalePercent(1);
    CHECK(clock.scalePercent() == 5);
}

TEST_CASE("opening another score stops the clock and clears the loop", "[transport][clock]")
{
    ClockUnderTest fixture;
    auto& clock = *fixture.clock;
    clock.setCountInBars(0);
    clock.setLoopEnabled(true);
    fixture.block();
    clock.poll();
    clock.play();
    fixture.blocks(20);

    fixture.currentScore.publish(scoreOf(8, {3, 4}, 90.0));
    clock.poll();
    fixture.block();
    clock.poll();

    CHECK_FALSE(clock.loopEnabled());
    CHECK(clock.position().state == ClockState::stopped);
    CHECK(clock.position().frame == 0);
}

TEST_CASE("with no score, play does nothing", "[transport][clock]")
{
    ClockUnderTest fixture(nullptr);
    auto& clock = *fixture.clock;
    CHECK_FALSE(clock.hasScore());
    clock.play();
    fixture.blocks(4);
    CHECK(clock.position().state == ClockState::stopped);
    CHECK_FALSE(clock.position().hasScore);
}

TEST_CASE("a later publish waits for a block, and commands wait behind it", "[transport][clock]")
{
    ClockUnderTest fixture;
    auto& clock = *fixture.clock;
    clock.setCountInBars(0);

    // The constructor's table has not been picked up, so this one waits...
    clock.setScalePercent(50);
    // ...and so does a seek, which has to be converted with it.
    clock.seek(4 * quarter);
    fixture.block();
    CHECK(clock.position().frame == 0);

    clock.poll();
    fixture.block();
    CHECK(clock.position().scalePercent == 50);
    CHECK(clock.position().frame == 4 * 44100);
}

TEST_CASE("the frame and time pair places an event on the timeline", "[transport][clock]")
{
    ClockUnderTest fixture;
    auto& clock = *fixture.clock;
    clock.setCountInBars(0);
    clock.play();
    fixture.blocks(10);

    const auto timing = clock.timing();
    CHECK(timing.sampleRate == 44100.0);
    CHECK(timing.position == 9 * 512);

    // 10 ms after the block began is 441 frames later.
    CHECK(timing.frameAt(timing.timeMilliseconds + 10.0) == timing.position + 441);
}

TEST_CASE("a locked clock ignores what would change a take", "[transport][clock][take]")
{
    ClockUnderTest fixture;
    auto& clock = *fixture.clock;
    clock.setCountInBars(0);
    fixture.block();

    clock.lock();
    CHECK(clock.isLocked());
    clock.setScalePercent(50);
    clock.setLoopEnabled(true);
    clock.setLoopRegion(0, 4 * quarter);
    clock.seek(8 * quarter);
    clock.seekToBar(3);
    fixture.block();

    CHECK(clock.scalePercent() == 100);
    CHECK_FALSE(clock.loopEnabled());
    CHECK(clock.position().frame == 0);

    // Play, pause and the metronome stay free.
    clock.play();
    fixture.blocks(2);
    CHECK(clock.position().state == ClockState::playing);
    clock.setMetronomeEnabled(false);
    CHECK_FALSE(clock.metronomeEnabled());

    clock.unlock();
    clock.setScalePercent(50);
    CHECK(clock.scalePercent() == 50);

    // The table a take keeps.
    const auto table = clock.currentTable();
    REQUIRE(table != nullptr);
    CHECK_FALSE(table->empty());
}
