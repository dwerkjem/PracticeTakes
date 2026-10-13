#pragma once

#include "GlyphMetrics.h"
#include "StaffPosition.h"

// Stems: which way one points, and where it runs from and to.
//
// Pure arithmetic over staff steps, like `StaffPosition.h`, and split out of
// the layout for the same reason: direction is a rule with three cases and a
// tie, and the tie is the one that gets written backwards. A test that says "a
// note on the middle line stems down" is worth more than the line of code it
// checks.
namespace score::engraving
{
// The line a stem's direction is decided against: the third line of a five-line
// staff.
inline constexpr int middleLineStep = 4;

// Default stem length, in staff steps. Seven steps is 3.5 staff spaces, which
// is an octave and is what every engraving manual gives as the default.
inline constexpr int defaultStemSteps = 7;

// How far a stem must clear the far notehead of a chord wider than an octave.
//
// The default length is measured from the notehead the stem *attaches* to, not
// from the one at its far end -- so a chord spanning more than an octave would
// otherwise end inside itself. One staff space past the far note is the
// minimum that still reads as a stem rather than as a line through a chord.
inline constexpr int chordOvershootSteps = 2;

// Which way a stem points, for an event with no voice to answer for it.
//
// The note furthest from the middle line decides, which for a single note is
// just that note. Equidistant, or on the line, points down: that is the
// convention, and it is the case worth pinning because either answer looks
// plausible in code.
[[nodiscard]] constexpr bool stemPointsUp(int lowestStep, int highestStep) noexcept
{
    const int lowDistance = middleLineStep - lowestStep;
    const int highDistance = highestStep - middleLineStep;

    if (highDistance > lowDistance)
    {
        return false;
    }

    if (lowDistance > highDistance)
    {
        return true;
    }

    // Equidistant. A chord straddling the middle line evenly, and a single note
    // sitting on it, both stem down.
    return false;
}

// A stem, as the layout places it.
struct StemGeometry
{
    // Centre of the stem, in staff spaces from the left edge of the system.
    double x = 0.0;

    // The notehead it attaches to, and its free end. `toStep` is a double
    // because a stem in a beamed group ends wherever the beam is, which is not
    // generally a whole step.
    double fromStep = 0.0;
    double toStep = 0.0;
};

// Where a stem runs, for one event already placed and already given a
// direction.
//
// `noteheadX` is the left edge of the notehead and `lowestStep`/`highestStep`
// the outermost notes of the chord -- equal for a single note.
//
// An up stem attaches at the *lowest* notehead and a down stem at the highest.
// The length is measured from the note it attaches to -- an octave -- which is
// the rule, and is not the same as "an octave past the far note": measuring
// from the far end gives a four-note chord a stem half the height of the
// system. A chord wider than an octave is the case where the two differ, and
// there the stem is extended just far enough to clear the far notehead.
//
// A note far outside the staff gets a longer stem, not a floating one: the free
// end is pulled back to the middle line so that every stem reaches the staff.
[[nodiscard]] StemGeometry
stemFor(double noteheadX, Glyph notehead, int lowestStep, int highestStep, bool up) noexcept;

// The flag glyph for an unbeamed event with this many flags, pointing this way.
//
// Only the eighth and sixteenth glyphs exist in the metrics table, so anything
// shorter reuses the sixteenth -- drawn, and reported by the caller, rather
// than drawn as an eighth and left to look deliberate.
[[nodiscard]] Glyph flagGlyphFor(int flags, bool up) noexcept;
} // namespace score::engraving
