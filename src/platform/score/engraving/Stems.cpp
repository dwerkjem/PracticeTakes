#include "Stems.h"

#include <algorithm>

namespace score::engraving
{
StemGeometry
stemFor(double noteheadX, Glyph notehead, int lowestStep, int highestStep, bool up) noexcept
{
    StemGeometry stem;

    const double width = advanceWidth(notehead);
    const double halfThickness = engravingDefaults.stemThickness * 0.5;

    if (up)
    {
        // On the right of the notehead, flush with its edge.
        stem.x = noteheadX + width - halfThickness;
        stem.fromStep = lowestStep;

        // An octave from the note it attaches to, or just clear of the top of
        // the chord when the chord is wider than that.
        stem.toStep = std::max(lowestStep + defaultStemSteps, highestStep + chordOvershootSteps);

        // Reaching the staff matters more than the nominal length. A note two
        // ledger lines below with a seven-step stem would stop short of the
        // staff and read as floating.
        stem.toStep = std::max(stem.toStep, static_cast<double>(middleLineStep));

        return stem;
    }

    stem.x = noteheadX + halfThickness;
    stem.fromStep = highestStep;
    stem.toStep = std::min(highestStep - defaultStemSteps, lowestStep - chordOvershootSteps);
    stem.toStep = std::min(stem.toStep, static_cast<double>(middleLineStep));

    return stem;
}

Glyph flagGlyphFor(int flags, bool up) noexcept
{
    if (flags <= 1)
    {
        return up ? Glyph::flag8thUp : Glyph::flag8thDown;
    }

    return up ? Glyph::flag16thUp : Glyph::flag16thDown;
}
} // namespace score::engraving
