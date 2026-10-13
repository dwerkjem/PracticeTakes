#pragma once

#include "platform/score/Pitch.h"
#include "platform/score/ScoreAttributes.h"

// Where a written pitch sits on a staff.
//
// The unit is the **staff step**: one diatonic degree, which is half a staff
// space, counted upward from the bottom line. So the bottom line is 0, the
// space above it 1, the second line 2, and the top line of a five-line staff
// is 8. Below the bottom line the numbers go negative, which is what ledger
// lines are made of.
//
// Steps rather than staff spaces because every vertical question notation asks
// is diatonic: a note sits on a line or in a space, never between them, and an
// accidental never moves it. Working in half-spaces makes "is this on a line"
// an evenness test instead of a floating-point comparison.
//
// No JUCE, and no font: this is pure arithmetic over the model's own spelling,
// which is why `Pitch` keeps step/alter/octave separately from the sounding
// number in the first place (Pitch.h says exactly this).
namespace score::engraving
{
// A five-line staff, bottom line 0 to top line 8.
inline constexpr int bottomLineStep = 0;
inline constexpr int topLineStep = 8;

// Position of a step name within its octave, C first. Distinct from
// `semitonesAboveC`: this counts *degrees*, so E to F is one step like D to E,
// even though it is one semitone rather than two.
[[nodiscard]] constexpr int diatonicIndexOf(Step step) noexcept
{
    switch (step)
    {
    case Step::c:
        return 0;
    case Step::d:
        return 1;
    case Step::e:
        return 2;
    case Step::f:
        return 3;
    case Step::g:
        return 4;
    case Step::a:
        return 5;
    case Step::b:
        return 6;
    }

    return 0;
}

// A pitch's absolute diatonic position: C4 is 28, D4 is 29, B3 is 27.
// Alteration is deliberately ignored -- F sharp and F natural occupy the same
// line, which is the whole reason the model keeps a spelling.
[[nodiscard]] constexpr int diatonicIndexOf(const Pitch& pitch) noexcept
{
    return (pitch.octave * 7) + diatonicIndexOf(pitch.step);
}

// The pitch a clef sign names, before its line is taken into account: a G clef
// names G4, an F clef names F3, a C clef names C4.
//
// Unknown signs -- "percussion", "TAB", "none" -- have no pitch, and the model
// keeps the sign verbatim precisely so they survive import. They fall back to
// treble here so that something is drawn; a renderer that refuses to place a
// percussion part would be worse than one that places it wrongly and says so.
[[nodiscard]] constexpr int clefReferenceDiatonicIndex(const Clef& clef) noexcept
{
    if (!clef.sign.empty())
    {
        switch (clef.sign.front())
        {
        case 'F':
            // F3.
            return (3 * 7) + 3;
        case 'C':
            // C4.
            return 4 * 7;
        default:
            break;
        }
    }

    // G4.
    return (4 * 7) + 4;
}

// The staff step a written pitch occupies under a clef.
//
// `Clef::line` counts staff lines from the bottom as 1, and the clef's
// reference pitch sits *on* that line -- a G clef on line 2 puts G4 on line 2,
// which is step 2. Everything else follows by diatonic distance.
//
// `Clef::octaveChange` is deliberately not applied. It changes what the staff
// *sounds*, not what it reads: the vocal tenor clef is written exactly like a
// treble clef and read an octave down, so applying it here would move every
// notehead off the line the engraver put it on. The model's own comment assigns
// it to playback, and `Note::sounding` already carries the result.
[[nodiscard]] constexpr int staffStepFor(const Pitch& pitch, const Clef& clef) noexcept
{
    return diatonicIndexOf(pitch) - clefReferenceDiatonicIndex(clef) + ((clef.line - 1) * 2);
}

// Whether a step falls on a printed staff line rather than in a space. Even
// steps are lines by construction.
[[nodiscard]] constexpr bool isOnLine(int staffStep) noexcept
{
    return (staffStep % 2) == 0;
}

// Ledger lines a notehead at this step needs, as the steps they are drawn at.
//
// Returned as a half-open range of *line* steps rather than a container, so
// that this stays constexpr and allocation-free: ledger lines are drawn every
// second step from just outside the staff up to and including the note's own
// step when that step is a line, and when the note sits in a space the
// outermost ledger line is the one below (or above) it.
struct LedgerLineRange
{
    // First ledger line step, inclusive. Equal to `endStep` when none are
    // needed.
    int beginStep = 0;

    // One past the last, counting in `stride`.
    int endStep = 0;

    // +2 above the staff, -2 below. Zero when there are none.
    int stride = 0;

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return stride == 0;
    }

    [[nodiscard]] constexpr int count() const noexcept
    {
        return stride == 0 ? 0 : (endStep - beginStep) / stride;
    }
};

[[nodiscard]] constexpr LedgerLineRange ledgerLinesFor(int staffStep) noexcept
{
    if (staffStep > topLineStep)
    {
        // A note in the space above a ledger line does not get its own line, so
        // round down to the nearest line step.
        const int outermost = staffStep - (isOnLine(staffStep) ? 0 : 1);

        return LedgerLineRange{topLineStep + 2, outermost + 2, 2};
    }

    if (staffStep < bottomLineStep)
    {
        const int outermost = staffStep + (isOnLine(staffStep) ? 0 : 1);

        return LedgerLineRange{bottomLineStep - 2, outermost - 2, -2};
    }

    return LedgerLineRange{0, 0, 0};
}
} // namespace score::engraving
