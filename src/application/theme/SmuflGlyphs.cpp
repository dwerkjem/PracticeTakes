#include "SmuflGlyphs.h"

#include <string>

namespace smufl
{
const std::vector<Glyph>& allGlyphs()
{
    static const std::vector<Glyph> glyphs{
        Glyph::brace,
        Glyph::noteheadDoubleWhole,
        Glyph::noteheadWhole,
        Glyph::noteheadHalf,
        Glyph::noteheadBlack,
        Glyph::gClef,
        Glyph::gClef15mb,
        Glyph::gClef8vb,
        Glyph::gClef8va,
        Glyph::gClef15ma,
        Glyph::cClef,
        Glyph::cClef8vb,
        Glyph::fClef,
        Glyph::fClef15mb,
        Glyph::fClef8vb,
        Glyph::fClef8va,
        Glyph::fClef15ma,
        Glyph::timeSig0,
        Glyph::timeSig1,
        Glyph::timeSig2,
        Glyph::timeSig3,
        Glyph::timeSig4,
        Glyph::timeSig5,
        Glyph::timeSig6,
        Glyph::timeSig7,
        Glyph::timeSig8,
        Glyph::timeSig9,
        Glyph::repeatDot,
        Glyph::augmentationDot,
        Glyph::flag8thUp,
        Glyph::flag8thDown,
        Glyph::flag16thUp,
        Glyph::flag16thDown,
        Glyph::flag32ndUp,
        Glyph::flag32ndDown,
        Glyph::flag64thUp,
        Glyph::flag64thDown,
        Glyph::flag128thUp,
        Glyph::flag128thDown,
        Glyph::accidentalFlat,
        Glyph::accidentalNatural,
        Glyph::accidentalSharp,
        Glyph::accidentalDoubleSharp,
        Glyph::accidentalDoubleFlat,
        Glyph::segno,
        Glyph::coda,
        Glyph::restDoubleWhole,
        Glyph::restWhole,
        Glyph::restHalf,
        Glyph::restQuarter,
        Glyph::rest8th,
        Glyph::rest16th,
        Glyph::rest32nd,
        Glyph::rest64th,
        Glyph::rest128th,
    };

    return glyphs;
}

std::optional<Glyph> flagFor(int flags, bool stemUp)
{
    // SMuFL pairs each flag as up then down at consecutive codepoints, an
    // eighth's first. Asserted pair by pair in SmuflGlyphsTests rather than
    // trusted as arithmetic.
    if (flags < 1 || flags > 5)
    {
        return std::nullopt;
    }

    const char32_t up = codepointOf(Glyph::flag8thUp) + static_cast<char32_t>((flags - 1) * 2);

    return static_cast<Glyph>(stemUp ? up : up + 1);
}

std::optional<ClefGlyphChoice> clefFor(const score::Clef& clef)
{
    const int octave = clef.octaveChange;

    if (clef.sign == "G")
    {
        switch (octave)
        {
        case 0:
            return ClefGlyphChoice{Glyph::gClef, true};
        case -1:
            return ClefGlyphChoice{Glyph::gClef8vb, true};
        case 1:
            return ClefGlyphChoice{Glyph::gClef8va, true};
        case -2:
            return ClefGlyphChoice{Glyph::gClef15mb, true};
        case 2:
            return ClefGlyphChoice{Glyph::gClef15ma, true};
        default:
            return ClefGlyphChoice{Glyph::gClef, false};
        }
    }

    if (clef.sign == "F")
    {
        switch (octave)
        {
        case 0:
            return ClefGlyphChoice{Glyph::fClef, true};
        case -1:
            return ClefGlyphChoice{Glyph::fClef8vb, true};
        case 1:
            return ClefGlyphChoice{Glyph::fClef8va, true};
        case -2:
            return ClefGlyphChoice{Glyph::fClef15mb, true};
        case 2:
            return ClefGlyphChoice{Glyph::fClef15ma, true};
        default:
            return ClefGlyphChoice{Glyph::fClef, false};
        }
    }

    if (clef.sign == "C")
    {
        if (octave == -1)
        {
            return ClefGlyphChoice{Glyph::cClef8vb, true};
        }

        return ClefGlyphChoice{Glyph::cClef, octave == 0};
    }

    return std::nullopt;
}

std::vector<Glyph> timeSignatureDigits(int value)
{
    std::vector<Glyph> digits;

    if (value < 0)
    {
        return digits;
    }

    for (const char digit : std::to_string(value))
    {
        digits.push_back(
            static_cast<Glyph>(codepointOf(Glyph::timeSig0) + static_cast<char32_t>(digit - '0')));
    }

    return digits;
}
} // namespace smufl
