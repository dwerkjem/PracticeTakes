#include <catch2/catch_test_macros.hpp>

#include "platform/midi/SoundingDurationJudgement.h"

using namespace midi;

namespace
{
// A note as the tracker would report it.
[[nodiscard]] SoundedNote note(
    double keyDown,
    double keyUp,
    double soundingEnd,
    SoundingEnd end = SoundingEnd::finger,
    PedalCertainty pedal = PedalCertainty::known)
{
    SoundedNote result;
    result.keyDownSeconds = keyDown;
    result.keyUpSeconds = keyUp;
    result.soundingEndSeconds = soundingEnd;
    result.end = end;
    result.pedal = pedal;
    return result;
}

constexpr double notated = 2.0;
} // namespace

TEST_CASE("released early but held by the pedal is not short", "[midi][judgement]")
{
    // Finger: 0.5s, well short of 2s. Sounding: 2s, because the pedal held it.
    const auto verdict = judgeSoundingDuration(notated, note(0.0, 0.5, 2.0, SoundingEnd::pedal));

    CHECK(verdict.outcome == DurationOutcome::onTarget);
    CHECK(verdict.heldBy == HeldBy::pedal);
}

TEST_CASE("released early with no pedal is short", "[midi][judgement]")
{
    const auto verdict = judgeSoundingDuration(notated, note(0.0, 0.5, 0.5));

    CHECK(verdict.outcome == DurationOutcome::tooShort);
    CHECK(verdict.heldBy == HeldBy::finger);
}

TEST_CASE("held by the finger beyond the notation is long", "[midi][judgement]")
{
    const auto verdict = judgeSoundingDuration(notated, note(0.0, 4.0, 4.0));

    CHECK(verdict.outcome == DurationOutcome::tooLong);
    CHECK(verdict.heldBy == HeldBy::finger);
}

TEST_CASE("held by the pedal beyond the notation is not long", "[midi][judgement]")
{
    // Ordinary pedalling. Calling this long would be the false negative this
    // exists to avoid, in the other direction.
    const auto verdict = judgeSoundingDuration(notated, note(0.0, 1.0, 6.0, SoundingEnd::pedal));

    CHECK(verdict.outcome == DurationOutcome::onTarget);
    CHECK(verdict.heldBy == HeldBy::pedal);
}

TEST_CASE("the tolerance edges fall on the stated side", "[midi][judgement]")
{
    constexpr double tolerance = 0.25; // 1.5s .. 2.5s around 2s

    SECTION("exactly at the lower edge is on target")
    {
        CHECK(
            judgeSoundingDuration(notated, note(0.0, 1.5, 1.5), tolerance).outcome ==
            DurationOutcome::onTarget);
    }

    SECTION("just below the lower edge is short")
    {
        CHECK(
            judgeSoundingDuration(notated, note(0.0, 1.49, 1.49), tolerance).outcome ==
            DurationOutcome::tooShort);
    }

    SECTION("exactly at the upper edge is on target")
    {
        CHECK(
            judgeSoundingDuration(notated, note(0.0, 2.5, 2.5), tolerance).outcome ==
            DurationOutcome::onTarget);
    }

    SECTION("just above the upper edge is long")
    {
        CHECK(
            judgeSoundingDuration(notated, note(0.0, 2.51, 2.51), tolerance).outcome ==
            DurationOutcome::tooLong);
    }
}

TEST_CASE("the default tolerance is the named constant", "[midi][judgement]")
{
    const SoundedNote played = note(0.0, 1.0, 1.0);

    CHECK(
        judgeSoundingDuration(notated, played).outcome ==
        judgeSoundingDuration(notated, played, defaultDurationTolerance).outcome);
}

TEST_CASE("a note whose duration is not known is not judged", "[midi][judgement]")
{
    SECTION("abandoned")
    {
        CHECK(
            judgeSoundingDuration(notated, note(0.0, 3.0, 3.0, SoundingEnd::abandoned)).outcome ==
            DurationOutcome::notJudged);
    }

    SECTION("pedal state unknown")
    {
        CHECK(
            judgeSoundingDuration(
                notated, note(0.0, 0.5, 0.5, SoundingEnd::finger, PedalCertainty::unknown))
                .outcome == DurationOutcome::notJudged);
    }

    SECTION("nothing to compare to")
    {
        CHECK(
            judgeSoundingDuration(0.0, note(0.0, 1.0, 1.0)).outcome == DurationOutcome::notJudged);
        CHECK(
            judgeSoundingDuration(-1.0, note(0.0, 1.0, 1.0)).outcome == DurationOutcome::notJudged);
    }
}
