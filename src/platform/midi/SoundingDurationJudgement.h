#pragma once

#include "NoteSoundingTracker.h"

// Whether a played note lasted as long as the score asked (no JUCE dependency).
//
// Judged by how long the note *sounded*, not how long the key was held. A
// pianist who lifts a finger early under a held pedal has played the note
// correctly, and calling it short would be the tool being wrong in a way the
// player cannot see -- worse than not judging duration at all.
//
// The notated duration is an input because producing it is #42's matching of a
// played note to a score note, which does not exist yet. Until it does, this is
// exercised by tests and not by the application.
namespace midi
{
// How far a sounding duration may stray from the notated one, as a fraction of
// it, before it is called short or long.
//
// A placeholder with a name, not a decision. How forgiving to be belongs to #44
// and #50, whose transparency requirement is that the user can see and change
// it; this is here so the number is findable when they arrive rather than buried
// as a literal in a function.
inline constexpr double defaultDurationTolerance = 0.25;

enum class DurationOutcome
{
    tooShort,
    onTarget,
    tooLong,
    // Declined, not zero: the note's duration is not known (see `NoteSoundingTracker`
    // on abandoned notes and unknown pedal state), or there is nothing to compare to.
    notJudged
};

enum class HeldBy
{
    finger,
    pedal
};

struct DurationVerdict
{
    DurationOutcome outcome = DurationOutcome::notJudged;
    HeldBy heldBy = HeldBy::finger;
};

[[nodiscard]] constexpr DurationVerdict judgeSoundingDuration(
    double notatedSeconds,
    const SoundedNote& note,
    double tolerance = defaultDurationTolerance) noexcept
{
    DurationVerdict verdict;
    verdict.heldBy = note.heldByPedal() ? HeldBy::pedal : HeldBy::finger;

    if (note.end == SoundingEnd::abandoned || note.pedal == PedalCertainty::unknown ||
        notatedSeconds <= 0.0)
    {
        return verdict;
    }

    const double sounding = note.soundingSeconds();

    if (sounding < notatedSeconds * (1.0 - tolerance))
    {
        verdict.outcome = DurationOutcome::tooShort;
    }
    else if (sounding > notatedSeconds * (1.0 + tolerance) && verdict.heldBy == HeldBy::finger)
    {
        // Only a finger-held note can be too long. A note ringing past its
        // notation because the pedal was down is ordinary pedalling, and calling
        // it long would reproduce the false negative this exists to avoid, in the
        // other direction.
        verdict.outcome = DurationOutcome::tooLong;
    }
    else
    {
        verdict.outcome = DurationOutcome::onTarget;
    }

    return verdict;
}
} // namespace midi
