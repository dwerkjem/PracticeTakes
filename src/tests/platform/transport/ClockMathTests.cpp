#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "platform/score/Score.h"
#include "platform/transport/ClockMath.h"

#include <memory>
#include <vector>

using Catch::Approx;
using namespace transport;

namespace
{
constexpr double rate = 44100.0;

// A score of `measures` bars of one time signature, with the given tempi. Only
// what the clock reads is filled in: measure starts, lengths and the meter.
[[nodiscard]] score::Score scoreOf(
    int measures,
    score::TimeSignature time,
    std::vector<score::TempoEntry> tempi,
    std::vector<std::pair<int, score::TimeSignature>> meterChanges = {})
{
    score::Score result;
    result.tempoMap = score::TempoMap::build(std::move(tempi));
    score::Part part;
    part.id = "P1";

    Tick start = 0;
    score::TimeSignature current = time;
    for (int index = 0; index < measures; ++index)
    {
        score::Measure measure;
        measure.index = static_cast<std::size_t>(index);
        measure.start = start;
        if (index == 0)
        {
            measure.attributes.time = time;
        }
        for (const auto& [at, changed] : meterChanges)
        {
            if (at == index)
            {
                measure.attributes.time = changed;
                current = changed;
            }
        }
        measure.nominalDuration = score::nominalTicks(current);
        start += measure.nominalDuration;
        part.measures.push_back(std::move(measure));
    }
    result.parts.push_back(std::move(part));
    return result;
}

[[nodiscard]] std::unique_ptr<ClockTable>
tableOf(const score::Score& source, int scalePercent = 100, Tick loopStart = 0, Tick loopEnd = 0)
{
    auto table = std::make_unique<ClockTable>();
    buildTable(*table, source, scalePercent, rate, loopStart, loopEnd);
    return table;
}

// Every click from `from` up to (not including) `until`.
[[nodiscard]] std::vector<Click> clicksBetween(const ClockTable& table, Frame from, Frame until)
{
    std::vector<Click> clicks;
    Frame cursor = from;
    while (cursor < until)
    {
        const Click click = nextClickAtOrAfter(table, cursor);
        if (click.frame >= until)
        {
            break;
        }
        clicks.push_back(click);
        cursor = click.frame + 1;
    }
    return clicks;
}

constexpr Tick quarter = score::ticksPerQuarterNote;
constexpr Tick eighth = quarter / 2;
} // namespace

TEST_CASE("the tempo scale is clamped to 5-200% in 5% steps", "[transport][clock]")
{
    CHECK(clampScalePercent(100) == 100);
    CHECK(clampScalePercent(5) == 5);
    CHECK(clampScalePercent(1) == 5);
    CHECK(clampScalePercent(0) == 5);
    CHECK(clampScalePercent(201) == 200);
    CHECK(clampScalePercent(72) == 70);
    CHECK(clampScalePercent(73) == 75);
    CHECK(clampCountInBars(-1) == 0);
    CHECK(clampCountInBars(9) == 4);
}

TEST_CASE("a tempo change is followed to the frame", "[transport][clock]")
{
    // 60 bpm for bars 1-4, then 120 bpm from bar 5 (tick 4 bars in).
    const Tick barFive = 4 * 4 * quarter;
    const auto source = scoreOf(8, {4, 4}, {{0, 60.0}, {barFive, 120.0}});
    const auto table = tableOf(source);

    REQUIRE(table->segmentCount == 2);
    CHECK_FALSE(table->truncated);

    const auto clicks = clicksBetween(*table, 0, frameOfTick(*table, 8.0 * 4 * quarter));
    REQUIRE(clicks.size() == 32);

    // A beat every second before bar 5, and every half second after it.
    for (std::size_t beat = 0; beat < 16; ++beat)
    {
        CHECK(clicks[beat].frame == static_cast<Frame>(beat) * 44100);
    }
    for (std::size_t beat = 16; beat < 32; ++beat)
    {
        CHECK(clicks[beat].frame == 16 * 44100 + static_cast<Frame>(beat - 16) * 22050);
    }

    // The downbeat of each bar is the accented click.
    for (std::size_t beat = 0; beat < clicks.size(); ++beat)
    {
        CHECK(clicks[beat].accented == (beat % 4 == 0));
        CHECK(clicks[beat].position.bar == static_cast<std::int64_t>(beat / 4));
        CHECK(clicks[beat].position.beat == static_cast<int>(beat % 4));
    }

    CHECK(table->endFrame == 16 * 44100 + 16 * 22050);
}

TEST_CASE("7/8 clicks seven eighths a bar with the first accented", "[transport][clock]")
{
    // 120 bpm to the quarter: an eighth is a quarter second.
    const auto source = scoreOf(3, {7, 8}, {{0, 120.0}});
    const auto table = tableOf(source);

    const auto clicks = clicksBetween(*table, 0, table->endFrame);
    REQUIRE(clicks.size() == 21);
    for (std::size_t beat = 0; beat < clicks.size(); ++beat)
    {
        CHECK(clicks[beat].frame == static_cast<Frame>(beat) * 11025);
        CHECK(clicks[beat].accented == (beat % 7 == 0));
        CHECK(clicks[beat].position.beat == static_cast<int>(beat % 7));
    }
}

TEST_CASE("a meter change starts a new bar grid", "[transport][clock]")
{
    // Two bars of 4/4, then 3/4.
    const auto source = scoreOf(4, {4, 4}, {{0, 60.0}}, {{2, {3, 4}}});
    const auto table = tableOf(source);

    const auto clicks = clicksBetween(*table, 0, table->endFrame);
    REQUIRE(clicks.size() == 4 + 4 + 3 + 3);
    CHECK(clicks[8].accented);
    CHECK(clicks[8].position.bar == 2);
    CHECK(clicks[11].accented);
    CHECK(clicks[11].position.bar == 3);
    CHECK(clicks[13].position.beat == 2);
}

TEST_CASE("a tempo change in the middle of a bar keeps the beat count", "[transport][clock]")
{
    // 4/4 at 60, changing to 120 on beat 3 of bar 2.
    const Tick change = 4 * quarter + 2 * quarter;
    const auto source = scoreOf(3, {4, 4}, {{0, 60.0}, {change, 120.0}});
    const auto table = tableOf(source);

    const auto clicks = clicksBetween(*table, 0, table->endFrame);
    REQUIRE(clicks.size() == 12);
    CHECK(clicks[6].frame == 6 * 44100);
    CHECK(clicks[6].position.bar == 1);
    CHECK(clicks[6].position.beat == 2);
    CHECK_FALSE(clicks[6].accented);
    CHECK(clicks[7].frame == 6 * 44100 + 22050);
    CHECK(clicks[8].accented);
    CHECK(clicks[8].position.bar == 2);
}

TEST_CASE("ticks and frames convert both ways at a scale", "[transport][clock]")
{
    const auto source = scoreOf(4, {4, 4}, {{0, 120.0}});

    const auto full = tableOf(source, 100);
    const auto half = tableOf(source, 50);

    // A quarter at 120 bpm is half a second; at 50% it is a whole second.
    CHECK(frameOfTick(*full, static_cast<double>(quarter)) == 22050);
    CHECK(frameOfTick(*half, static_cast<double>(quarter)) == 44100);
    CHECK(tickAtFrame(*half, 44100) == Approx(static_cast<double>(quarter)));
    CHECK(tickAtFrame(*full, 11025) == Approx(static_cast<double>(eighth)));

    // At 5%, the slowest scale, a quarter lasts ten seconds.
    const auto slowest = tableOf(source, 1);
    CHECK(slowest->scalePercent == 5);
    CHECK(frameOfTick(*slowest, static_cast<double>(quarter)) == 441000);
}

TEST_CASE("the click lands on the beat at half speed", "[transport][clock]")
{
    const auto source = scoreOf(2, {4, 4}, {{0, 120.0}});
    const auto table = tableOf(source, 50);

    const auto clicks = clicksBetween(*table, 0, frameOfTick(*table, 4.0 * quarter));
    REQUIRE(clicks.size() == 4);
    for (std::size_t beat = 0; beat < 4; ++beat)
    {
        CHECK(clicks[beat].frame == static_cast<Frame>(beat) * 44100);
    }
}

TEST_CASE("a scale change mid-bar continues from the same beat", "[transport][clock]")
{
    // 120 bpm 4/4. At beat 3 of bar 6 the scale goes from 100% to 70%.
    const auto source = scoreOf(10, {4, 4}, {{0, 120.0}});
    const auto full = tableOf(source, 100);
    const auto slower = tableOf(source, 70);

    const Tick beatThreeOfBarSix = 5 * 4 * quarter + 2 * quarter;
    // A little after beat 3, as a position would be mid-block.
    const Frame before = frameOfTick(*full, static_cast<double>(beatThreeOfBarSix)) + 100;
    const Frame after = reanchor(*full, *slower, before);

    // Same place in the score.
    CHECK(tickAtFrame(*slower, after) == Approx(tickAtFrame(*full, before)).margin(1.0));

    // The next click is beat 4 of bar 6, one 70% beat after beat 3.
    const Click next = nextClickAtOrAfter(*slower, after);
    CHECK(next.position.bar == 5);
    CHECK(next.position.beat == 3);
    const double beatAtSeventy = 0.5 / 0.7 * rate;
    const Frame beatThreeAtSeventy = frameOfTick(*slower, static_cast<double>(beatThreeOfBarSix));
    CHECK(next.frame == beatThreeAtSeventy + roundToFrame(beatAtSeventy));
    CHECK(next.frame > after);
}

TEST_CASE("a loop's bounds are whole frames and a wrap is exact", "[transport][clock]")
{
    const auto source = scoreOf(8, {4, 4}, {{0, 120.0}});
    const auto table = tableOf(source, 100, 2 * 4 * quarter, 5 * 4 * quarter);

    REQUIRE(table->loopEnabled);
    CHECK(table->loopStartFrame == 2 * 4 * 22050);
    CHECK(table->loopEndFrame == 5 * 4 * 22050);

    Frame position = table->loopEndFrame;
    CHECK(wrapIntoLoop(*table, position) == 1);
    CHECK(position == table->loopStartFrame);

    position = table->loopEndFrame + 17;
    CHECK(wrapIntoLoop(*table, position) == 1);
    CHECK(position == table->loopStartFrame + 17);

    position = table->loopEndFrame - 1;
    CHECK(wrapIntoLoop(*table, position) == 0);

    // A loop with no length is no loop.
    const auto empty = tableOf(source, 100, 4 * quarter, 4 * quarter);
    CHECK_FALSE(empty->loopEnabled);
}

TEST_CASE(
    "ten thousand wraps of a loop in 7/8 with a tempo change inside it start on time",
    "[transport][clock]")
{
    // Bars 3-5 of a piece in 7/8, with the tempo going from 97 to 133 bpm in the
    // middle of bar 4: an awkward rate, an odd meter, a change inside the loop.
    const Tick bar = 7 * eighth;
    const Tick change = 3 * bar + 3 * eighth;
    const auto source = scoreOf(8, {7, 8}, {{0, 97.0}, {change, 133.0}});
    const auto table = tableOf(source, 100, 2 * bar, 5 * bar);
    REQUIRE(table->loopEnabled);

    const Frame length = table->loopEndFrame - table->loopStartFrame;
    const auto firstClicks = clicksBetween(*table, table->loopStartFrame, table->loopEndFrame);
    REQUIRE(firstClicks.size() == 21);

    // Play it as the renderer does: blocks of 512, wrapping where the loop ends.
    Frame position = table->loopStartFrame;
    std::int64_t elapsed = 0;
    std::int64_t wraps = 0;
    std::int64_t lastWrapElapsed = 0;
    constexpr Frame block = 512;
    bool everyRepetitionOnTime = true;
    bool everyIntervalExact = true;

    while (wraps < 10000)
    {
        Frame remaining = block;
        while (remaining > 0)
        {
            const Frame untilEnd = table->loopEndFrame - position;
            const Frame step = std::min(remaining, untilEnd);
            position += step;
            elapsed += step;
            remaining -= step;
            if (wrapIntoLoop(*table, position) > 0)
            {
                ++wraps;
                everyRepetitionOnTime = everyRepetitionOnTime && position == table->loopStartFrame;
                if (wraps > 1)
                {
                    everyIntervalExact = everyIntervalExact && elapsed - lastWrapElapsed == length;
                }
                lastWrapElapsed = elapsed;
            }
        }
    }

    CHECK(everyRepetitionOnTime);
    CHECK(everyIntervalExact);
    CHECK(elapsed - lastWrapElapsed < block);

    // And the beats inside the last repetition are where they were in the first.
    const auto lastClicks = clicksBetween(*table, table->loopStartFrame, table->loopEndFrame);
    REQUIRE(lastClicks.size() == firstClicks.size());
    for (std::size_t index = 0; index < firstClicks.size(); ++index)
    {
        CHECK(lastClicks[index].frame == firstClicks[index].frame);
    }
    CHECK(firstClicks.front().accented);
    CHECK(firstClicks.front().position.bar == 2);
}

TEST_CASE("one bar of 3/4 counts in three clicks, the first accented", "[transport][clock]")
{
    const auto source = scoreOf(4, {3, 4}, {{0, 90.0}});
    const auto table = tableOf(source);

    const CountIn countIn = countInBefore(*table, 0, 1);
    CHECK(countIn.beats == 3);
    CHECK(countIn.targetFrame == 0);
    const double beat = 60.0 / 90.0 * rate;
    CHECK(countIn.framesPerBeat == Approx(beat));
    CHECK(countIn.startFrame == -roundToFrame(3 * beat));

    std::vector<Click> clicks;
    Click click;
    Frame cursor = countIn.startFrame;
    while (nextCountInClick(countIn, cursor, click))
    {
        clicks.push_back(click);
        cursor = click.frame + 1;
    }

    REQUIRE(clicks.size() == 3);
    CHECK(clicks[0].frame == countIn.startFrame);
    CHECK(clicks[0].accented);
    CHECK_FALSE(clicks[1].accented);
    CHECK_FALSE(clicks[2].accented);
    CHECK(clicks[0].position.bar == -1);

    // The third is followed one beat later by position zero.
    CHECK(countIn.targetFrame - clicks[2].frame == roundToFrame(beat));
}

TEST_CASE("a count-in before a later bar uses that point's tempo and meter", "[transport][clock]")
{
    // Two bars of 4/4 at 60, then 3/4 at 120: counting in to bar 3.
    const Tick barThree = 2 * 4 * quarter;
    const auto source = scoreOf(4, {4, 4}, {{0, 60.0}, {barThree, 120.0}}, {{2, {3, 4}}});
    const auto table = tableOf(source);

    const CountIn countIn = countInBefore(*table, barThree, 2);
    CHECK(countIn.beats == 6);
    CHECK(countIn.beatsPerBar == 3);
    CHECK(countIn.targetFrame == 8 * 44100);
    CHECK(countIn.startFrame == 8 * 44100 - 6 * 22050);

    // Zero bars is no count-in.
    CHECK(countInBefore(*table, barThree, 0).beats == 0);
}

TEST_CASE("more than 512 changes keeps the first 512 and says so", "[transport][clock]")
{
    std::vector<score::TempoEntry> tempi;
    for (int index = 0; index < 600; ++index)
    {
        tempi.push_back({static_cast<Tick>(index) * quarter, 60.0 + index % 7});
    }
    const auto source = scoreOf(150, {4, 4}, tempi);
    const auto table = tableOf(source);

    CHECK(table->segmentCount == maximumSegments);
    CHECK(table->truncated);

    // What is kept still plays.
    const auto clicks = clicksBetween(*table, 0, 44100 * 10);
    CHECK_FALSE(clicks.empty());
}

TEST_CASE("an empty score still has a beat", "[transport][clock]")
{
    const score::Score source;
    const auto table = tableOf(source);

    REQUIRE(table->segmentCount == 1);
    CHECK(table->endFrame == 0);
    const auto clicks = clicksBetween(*table, 0, 44100);
    REQUIRE(clicks.size() == 2);
    CHECK(clicks[1].frame == 22050);
}

TEST_CASE("bar and beat at a frame", "[transport][clock]")
{
    const auto source = scoreOf(4, {3, 4}, {{0, 60.0}});
    const auto table = tableOf(source);

    CHECK(beatAtFrame(*table, 0).bar == 0);
    CHECK(beatAtFrame(*table, 0).accented);
    CHECK(beatAtFrame(*table, 44100 * 4 + 10).bar == 1);
    CHECK(beatAtFrame(*table, 44100 * 4 + 10).beat == 1);
    CHECK_FALSE(beatAtFrame(*table, 44100 * 4 + 10).accented);

    Click click;
    CHECK(clickStartsIn(*table, 44100 - 5, 10, click));
    CHECK(click.frame == 44100);
    CHECK_FALSE(clickStartsIn(*table, 44101, 1000, click));
}
