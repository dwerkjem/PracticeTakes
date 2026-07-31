#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "services/score/TempoMap.h"

using Catch::Approx;
using namespace score;

namespace
{
// One bar of 4/4.
constexpr Tick oneBar = ticksPerQuarterNote * 4;
} // namespace

TEST_CASE("a default tempo map is 120 BPM from the start", "[score][tempo]")
{
    const TempoMap map;

    REQUIRE(map.entries().size() == 1);
    CHECK(map.entries().front().tick == 0);
    CHECK(map.entries().front().beatsPerMinute == Approx(defaultBeatsPerMinute));
}

TEST_CASE("a single tempo converts ticks to seconds linearly", "[score][tempo]")
{
    const TempoMap map = TempoMap::build({{0, 60.0}});

    // At 60 BPM a quarter note is exactly one second.
    CHECK(map.tickToSeconds(0) == Approx(0.0));
    CHECK(map.tickToSeconds(ticksPerQuarterNote) == Approx(1.0));
    CHECK(map.tickToSeconds(oneBar) == Approx(4.0));

    // At 120 BPM it is half a second.
    const TempoMap faster = TempoMap::build({{0, 120.0}});

    CHECK(faster.tickToSeconds(ticksPerQuarterNote) == Approx(0.5));
    CHECK(faster.tickToSeconds(oneBar) == Approx(2.0));
}

TEST_CASE("a mid-score tempo change applies from its own position", "[score][tempo]")
{
    // 60 BPM for one bar, then 120 BPM.
    const TempoMap map = TempoMap::build({{0, 60.0}, {oneBar, 120.0}});

    CHECK(map.beatsPerMinuteAt(0) == Approx(60.0));
    CHECK(map.beatsPerMinuteAt(oneBar - 1) == Approx(60.0));
    CHECK(map.beatsPerMinuteAt(oneBar) == Approx(120.0));
    CHECK(map.beatsPerMinuteAt(oneBar * 4) == Approx(120.0));

    // The first bar takes four seconds, the second takes two -- the change is
    // applied segment by segment rather than by approximating with one tempo.
    CHECK(map.tickToSeconds(oneBar) == Approx(4.0));
    CHECK(map.tickToSeconds(oneBar * 2) == Approx(6.0));
    CHECK(map.tickToSeconds(oneBar * 3) == Approx(8.0));
}

TEST_CASE("several tempo changes accumulate exactly", "[score][tempo]")
{
    const TempoMap map =
        TempoMap::build({{0, 60.0}, {oneBar, 120.0}, {oneBar * 2, 240.0}, {oneBar * 3, 30.0}});

    CHECK(map.tickToSeconds(oneBar) == Approx(4.0));
    CHECK(map.tickToSeconds(oneBar * 2) == Approx(6.0));
    CHECK(map.tickToSeconds(oneBar * 3) == Approx(7.0));
    CHECK(map.tickToSeconds(oneBar * 4) == Approx(15.0));
}

TEST_CASE("a file that declares no tempo gets the documented default", "[score][tempo]")
{
    // Invariant 8: the map is never empty, so no consumer has to handle the
    // "no tempo anywhere" case. Plenty of engraved vocal scores have no
    // marking at all.
    const TempoMap map = TempoMap::build({});

    REQUIRE(map.entries().size() == 1);
    CHECK(map.entries().front().tick == 0);
    CHECK(map.beatsPerMinuteAt(0) == Approx(defaultBeatsPerMinute));
    CHECK(map.tickToSeconds(oneBar) == Approx(2.0));
}

TEST_CASE("a map whose first tempo starts late is covered from tick zero", "[score][tempo]")
{
    // A file whose only <sound tempo> sits in bar 5 still has to answer "how
    // long is bar 1".
    const TempoMap map = TempoMap::build({{oneBar * 4, 60.0}});

    REQUIRE(map.entries().size() == 2);
    CHECK(map.entries().front().tick == 0);
    CHECK(map.beatsPerMinuteAt(0) == Approx(defaultBeatsPerMinute));
    CHECK(map.beatsPerMinuteAt(oneBar * 4) == Approx(60.0));
}

TEST_CASE("duplicate positions collapse to the last declaration", "[score][tempo]")
{
    // Invariant 8 forbids duplicate ticks. A later declaration in the file is
    // the one the engraver meant, so it wins.
    const TempoMap map = TempoMap::build({{0, 60.0}, {oneBar, 90.0}, {oneBar, 180.0}});

    REQUIRE(map.entries().size() == 2);
    CHECK(map.entries()[1].tick == oneBar);
    CHECK(map.entries()[1].beatsPerMinute == Approx(180.0));
}

TEST_CASE("entries are sorted regardless of the order they arrive in", "[score][tempo]")
{
    const TempoMap map = TempoMap::build({{oneBar * 2, 240.0}, {0, 60.0}, {oneBar, 120.0}});

    REQUIRE(map.entries().size() == 3);
    CHECK(map.entries()[0].tick == 0);
    CHECK(map.entries()[1].tick == oneBar);
    CHECK(map.entries()[2].tick == oneBar * 2);
}

TEST_CASE("meaningless tempi are dropped rather than propagated", "[score][tempo]")
{
    const TempoMap map = TempoMap::build({{0, 0.0}, {oneBar, -60.0}, {oneBar * 2, 90.0}});

    // The two unusable entries go; a default is inserted to cover the start.
    REQUIRE(map.entries().size() == 2);
    CHECK(map.entries()[0].tick == 0);
    CHECK(map.entries()[0].beatsPerMinute == Approx(defaultBeatsPerMinute));
    CHECK(map.entries()[1].beatsPerMinute == Approx(90.0));
}

TEST_CASE("position round-trips through seconds and back", "[score][tempo]")
{
    const TempoMap map = TempoMap::build({{0, 72.0}, {oneBar, 144.0}, {oneBar * 3, 96.0}});

    for (const Tick tick :
         {Tick{0}, ticksPerQuarterNote, oneBar, oneBar + ticksPerQuarterNote, oneBar * 2,
          oneBar * 3, oneBar * 5})
    {
        INFO("tick " << tick);
        CHECK(map.secondsToTick(map.tickToSeconds(tick)) == tick);
    }
}

TEST_CASE("seconds round-trip through position and back", "[score][tempo]")
{
    const TempoMap map = TempoMap::build({{0, 60.0}, {oneBar, 120.0}});

    for (const double seconds : {0.0, 1.0, 2.5, 4.0, 6.0, 10.0})
    {
        INFO("seconds " << seconds);
        CHECK(map.tickToSeconds(map.secondsToTick(seconds)) == Approx(seconds).margin(0.001));
    }
}

TEST_CASE("time before the start of the score clamps to tick zero", "[score][tempo]")
{
    const TempoMap map = TempoMap::build({{0, 120.0}});

    CHECK(map.secondsToTick(-1.0) == 0);
    CHECK(map.secondsToTick(0.0) == 0);
}
