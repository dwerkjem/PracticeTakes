#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "features/nownext/NowNextModel.h"
#include "platform/transport/AudibleTick.h"

using Catch::Approx;
using namespace nownext;

namespace
{
constexpr score::Tick quarter = score::ticksPerQuarterNote;

// Eight quarter items alternating G4 and A4, with a rest at the fourth.
[[nodiscard]] score::PartTimeline eight()
{
    score::PartTimeline timeline;
    for (int index = 0; index < 8; ++index)
    {
        score::TimelineItem item;
        item.start = index * quarter;
        item.end = (index + 1) * quarter;
        if (index != 3)
        {
            item.kind = score::TimelineKind::note;
            score::Pitch pitch;
            pitch.midiNoteNumber = index % 2 == 0 ? 67 : 69;
            item.pitches.push_back(pitch);
        }
        timeline.push_back(item);
    }
    return timeline;
}

[[nodiscard]] score::Score fourFour(double bpm)
{
    score::Score source;
    source.tempoMap = score::TempoMap::build({{0, bpm}});
    score::Part part;
    score::Measure measure;
    measure.nominalDuration = 4 * quarter;
    measure.attributes.time = score::TimeSignature{4, 4};
    part.measures.push_back(measure);
    source.parts.push_back(part);
    return source;
}
} // namespace

TEST_CASE("now is the item sounding, and next the items after it", "[nownext]")
{
    const View view = viewAt(eight(), 1.5 * quarter, 4);
    CHECK(view.now == 1U);
    CHECK(view.next == std::vector<std::size_t>{2, 3, 4, 5});
    CHECK(view.through == Approx(0.5));
}

TEST_CASE("the look-ahead stops at the end", "[nownext]")
{
    const View view = viewAt(eight(), 6.2 * quarter, 4);
    CHECK(view.now == 6U);
    CHECK(view.next == std::vector<std::size_t>{7});
}

TEST_CASE("before the first item there is no now, only next", "[nownext]")
{
    // A count-in: the tick is negative.
    const View view = viewAt(eight(), -2.0 * quarter, 2);
    CHECK_FALSE(view.now.has_value());
    CHECK(view.next == std::vector<std::size_t>{0, 1});
    CHECK(view.through == 0.0);
}

TEST_CASE("the audible tick is the clock's while it rests", "[nownext][transport]")
{
    transport::ClockTable table;
    transport::buildTable(table, fourFour(120.0), 100, 44100.0);
    transport::ClockTiming timing;
    timing.position = 44100;
    timing.timeMilliseconds = 1000.0;
    timing.sampleRate = 44100.0;
    CHECK(transport::audibleTick(480.0, false, timing, &table, 5000.0, 512) == 480.0);
    CHECK(transport::audibleTick(480.0, true, timing, nullptr, 5000.0, 512) == 480.0);
}

TEST_CASE(
    "while playing, the audible tick runs on from the clock, less the latency",
    "[nownext][transport]")
{
    // 120 bpm: a quarter is half a second, 22,050 frames.
    transport::ClockTable table;
    transport::buildTable(table, fourFour(120.0), 100, 44100.0);
    transport::ClockTiming timing;
    timing.position = 44100; // two quarters in
    timing.timeMilliseconds = 1000.0;
    timing.sampleRate = 44100.0;

    // A quarter of a second later, with no latency: two and a half quarters.
    CHECK(
        transport::audibleTick(0.0, true, timing, &table, 1250.0, 0) ==
        Approx(2.5 * quarter).margin(1.0));

    // With a quarter of a second of output latency, still at two quarters.
    CHECK(
        transport::audibleTick(0.0, true, timing, &table, 1250.0, 11025) ==
        Approx(2.0 * quarter).margin(1.0));
}

TEST_CASE("an item's beats and seconds, at the table's meter and scale", "[nownext][transport]")
{
    transport::ClockTable table;
    transport::buildTable(table, fourFour(120.0), 50, 44100.0);
    CHECK(transport::beatsBetween(table, 0.0, 3.0 * quarter) == Approx(3.0));

    // At half speed a quarter at 120 takes a second.
    CHECK(transport::secondsBetween(table, 0.0, quarter) == Approx(1.0).margin(0.001));
    CHECK(transport::secondsBetween(table, quarter, 0.0) == 0.0);
}

TEST_CASE("beats are said with common fractions", "[nownext]")
{
    CHECK(beatsText(1.0) == "1 beat");
    CHECK(beatsText(2.0) == "2 beats");
    CHECK(beatsText(0.75) == "\xC2\xBE beat");
    CHECK(beatsText(0.25) == "\xC2\xBC beat");
    CHECK(beatsText(1.5) == "1\xC2\xBD beats");
    CHECK(beatsText(1.0 / 3.0) == "\xE2\x85\x93 beat");
    CHECK(beatsText(0.37) == "0.4 beat");
}
