#include <catch2/catch_test_macros.hpp>

#include "services/score/MusicalTime.h"

using namespace score;

TEST_CASE("the tick base divides the subdivisions real scores use", "[score][time]")
{
    // The whole justification for 3840 (design decision 2) is that common
    // subdivisions land on integer ticks. If this ever fails, durations start
    // rounding and the rest of the model's arithmetic stops being exact.
    CHECK(ticksPerQuarterNote == 3840);

    for (const Tick divisor : {2, 3, 4, 5, 6, 8, 12, 16, 24, 32, 64, 128, 256})
    {
        INFO("divisor " << divisor);
        CHECK(ticksPerQuarterNote % divisor == 0);
    }
}

TEST_CASE("rescaling reports exactness for the common divisions values", "[score][time]")
{
    // A quarter note is one quarter note whatever the source called it.
    for (const Tick divisions : {1, 2, 4, 8, 24, 96, 480, 960})
    {
        INFO("divisions " << divisions);

        const RescaledDuration quarter = rescaleToTicks(divisions, divisions);

        CHECK(quarter.ticks == ticksPerQuarterNote);
        CHECK(quarter.exact);
    }
}

TEST_CASE("rescaling converts note values against a typical divisions", "[score][time]")
{
    constexpr Tick divisions = 480;

    CHECK(rescaleToTicks(divisions * 4, divisions).ticks == ticksPerWholeNote);
    CHECK(rescaleToTicks(divisions * 2, divisions).ticks == ticksPerQuarterNote * 2);
    CHECK(rescaleToTicks(divisions / 2, divisions).ticks == ticksPerQuarterNote / 2);
    CHECK(rescaleToTicks(divisions / 4, divisions).ticks == ticksPerQuarterNote / 4);

    // An eighth-note triplet: three in the time of one quarter note.
    const RescaledDuration triplet = rescaleToTicks(divisions / 3, divisions);

    CHECK(triplet.ticks == ticksPerQuarterNote / 3);
    CHECK(triplet.exact);
}

TEST_CASE("an inexact rescale rounds and says so", "[score][time]")
{
    // 7 divisions per quarter note does not divide 3840, so one seventh of a
    // quarter note cannot be represented exactly. What matters is that the
    // caller is told, so it can emit a diagnostic rather than accumulate drift
    // silently.
    const RescaledDuration seventh = rescaleToTicks(1, 7);

    CHECK_FALSE(seventh.exact);

    // 3840 / 7 == 548.57..., so nearest is 549.
    CHECK(seventh.ticks == 549);
}

TEST_CASE("rescaling rounds half away from zero", "[score][time]")
{
    // 3840 / 512 == 7.5 exactly, so 1 unit at 512 divisions is a half-tick.
    const RescaledDuration positiveHalf = rescaleToTicks(1, 512);

    CHECK_FALSE(positiveHalf.exact);
    CHECK(positiveHalf.ticks == 8);

    const RescaledDuration negativeHalf = rescaleToTicks(-1, 512);

    CHECK_FALSE(negativeHalf.exact);
    CHECK(negativeHalf.ticks == -8);
}

TEST_CASE("a zero duration rescales exactly whatever the divisions", "[score][time]")
{
    CHECK(rescaleToTicks(0, 7).ticks == 0);
    CHECK(rescaleToTicks(0, 7).exact);
}

TEST_CASE("a non-positive divisions is malformed input, not a crash", "[score][time]")
{
    // <divisions>0</divisions> occurs in the wild. Dividing by it must not be
    // how we find that out.
    for (const Tick divisions : {0, -1, -480})
    {
        INFO("divisions " << divisions);

        const RescaledDuration result = rescaleToTicks(960, divisions);

        CHECK(result.ticks == 0);
        CHECK_FALSE(result.exact);
    }
}

TEST_CASE("a backup's negative duration rescales symmetrically", "[score][time]")
{
    // <backup> moves the cursor back, so negative durations reach this code.
    constexpr Tick divisions = 480;

    const RescaledDuration back = rescaleToTicks(-divisions * 2, divisions);

    CHECK(back.ticks == -ticksPerQuarterNote * 2);
    CHECK(back.exact);
}

TEST_CASE("nominal measure length follows the time signature", "[score][time]")
{
    CHECK(nominalMeasureTicks(4, 4) == ticksPerWholeNote);
    CHECK(nominalMeasureTicks(3, 4) == ticksPerQuarterNote * 3);
    CHECK(nominalMeasureTicks(2, 2) == ticksPerWholeNote);

    // 6/8 is six eighth notes, not six quarter notes.
    CHECK(nominalMeasureTicks(6, 8) == ticksPerQuarterNote * 3);

    // 5/4 and 7/8 are ordinary in the repertoire this application targets.
    CHECK(nominalMeasureTicks(5, 4) == ticksPerQuarterNote * 5);
    CHECK(nominalMeasureTicks(7, 8) == ticksPerQuarterNote * 7 / 2);
}

TEST_CASE("a malformed time signature yields zero rather than dividing by zero", "[score][time]")
{
    CHECK(nominalMeasureTicks(0, 4) == 0);
    CHECK(nominalMeasureTicks(4, 0) == 0);
    CHECK(nominalMeasureTicks(-4, 4) == 0);
}
