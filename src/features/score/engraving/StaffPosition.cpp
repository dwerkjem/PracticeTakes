#include "StaffPosition.h"

namespace engraving
{
namespace
{
// Diatonic value of a spelling: steps above C0, counting only letter names. One
// unit here is one staff position, which is what makes the arithmetic below a
// subtraction rather than a table.
[[nodiscard]] int diatonicValue(score::Step step, int octave)
{
    return (octave * 7) + score::diatonicIndex(step);
}

[[nodiscard]] int diatonicValue(const score::Pitch& pitch)
{
    return diatonicValue(pitch.step, pitch.octave);
}
} // namespace

std::optional<score::Pitch> clefReferencePitch(const score::Clef& clef)
{
    // The sign names the pitch the glyph centres on. These three are the whole
    // of the pitched clefs MusicXML defines; everything else -- percussion,
    // tablature, an explicit none -- names no pitch.
    if (clef.sign == "G")
    {
        return score::pitchFromSpelling(score::Step::g, 0, 4);
    }

    if (clef.sign == "F")
    {
        return score::pitchFromSpelling(score::Step::f, 0, 3);
    }

    if (clef.sign == "C")
    {
        return score::pitchFromSpelling(score::Step::c, 0, 4);
    }

    return std::nullopt;
}

bool isPitchedClef(const score::Clef& clef)
{
    return clefReferencePitch(clef).has_value();
}

std::optional<StaffSteps> staffPositionFor(const score::Clef& clef, const score::Pitch& pitch)
{
    const std::optional<score::Pitch> reference = clefReferencePitch(clef);

    if (!reference.has_value())
    {
        return std::nullopt;
    }

    // Undo the clef's octave displacement: see the header. An octave is seven
    // diatonic steps, so -1 means read the pitch seven steps higher than it
    // sounds.
    const int asRead = diatonicValue(pitch) - (clef.octaveChange * 7);

    const StaffSteps referenceSteps = stepsForLineFromBottom(clef.line);

    // Downwards on the staff is upwards in half spaces, so the difference is
    // subtracted: a pitch a step above the reference is one half space higher,
    // which is one *less*.
    return referenceSteps - (asRead - diatonicValue(*reference));
}

LedgerLines ledgerLinesFor(StaffSteps steps)
{
    LedgerLines lines;

    if (steps <= topLineSteps - 2)
    {
        // Lines sit at even positions, so a note at -2 or -3 shares the first
        // ledger line above and a note at -4 or -5 the second.
        lines.above = (topLineSteps - steps) / 2;
    }

    if (steps >= bottomLineSteps + 2)
    {
        lines.below = (steps - bottomLineSteps) / 2;
    }

    return lines;
}

bool stemPointsUpFor(StaffSteps steps)
{
    return steps > middleLineSteps;
}
} // namespace engraving
