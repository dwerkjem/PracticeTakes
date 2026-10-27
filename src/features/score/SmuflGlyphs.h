#pragma once

#include <array>
#include <optional>
#include <vector>

#include "engraving/Accidental.h"
#include "engraving/NoteValue.h"
#include "platform/score/ScoreAttributes.h"

// Which music-font glyph draws each thing the layout names (no JUCE
// dependency).
//
// SMuFL -- the Standard Music Font Layout -- assigns every notation symbol a
// fixed codepoint in the Unicode Private Use Area, so any conforming font draws
// the same symbol for the same number. Bravura, the font this application
// embeds, is SMuFL's reference font. Keeping the mapping here, as data and
// small pure functions, means the choice of glyph is unit tested without a
// font, a display or JUCE: whether a sixteenth's up-stem takes `flag16thUp` is
// a fact about notation, and it should not take a rendered picture to check.
//
// Only what the renderer draws is listed. SMuFL defines some 3,000 glyphs, and
// a table of the ones nothing uses would be a table nobody can check.
namespace smufl
{
// SMuFL's names, which are what its specification and every font's metadata
// use, so a reader can look any of them up.
enum class Glyph : char32_t
{
    noteheadDoubleWhole = 0xE0A0,
    noteheadWhole = 0xE0A2,
    noteheadHalf = 0xE0A3,
    noteheadBlack = 0xE0A4,

    gClef = 0xE050,
    gClef15mb = 0xE051,
    gClef8vb = 0xE052,
    gClef8va = 0xE053,
    gClef15ma = 0xE054,
    cClef = 0xE05C,
    cClef8vb = 0xE05D,
    fClef = 0xE062,
    fClef15mb = 0xE063,
    fClef8vb = 0xE064,
    fClef8va = 0xE065,
    fClef15ma = 0xE066,

    timeSig0 = 0xE080,
    timeSig1 = 0xE081,
    timeSig2 = 0xE082,
    timeSig3 = 0xE083,
    timeSig4 = 0xE084,
    timeSig5 = 0xE085,
    timeSig6 = 0xE086,
    timeSig7 = 0xE087,
    timeSig8 = 0xE088,
    timeSig9 = 0xE089,

    repeatDot = 0xE044,

    augmentationDot = 0xE1E7,

    flag8thUp = 0xE240,
    flag8thDown = 0xE241,
    flag16thUp = 0xE242,
    flag16thDown = 0xE243,
    flag32ndUp = 0xE244,
    flag32ndDown = 0xE245,
    flag64thUp = 0xE246,
    flag64thDown = 0xE247,
    flag128thUp = 0xE248,
    flag128thDown = 0xE249,

    accidentalFlat = 0xE260,
    accidentalNatural = 0xE261,
    accidentalSharp = 0xE262,
    accidentalDoubleSharp = 0xE263,
    accidentalDoubleFlat = 0xE264,

    restDoubleWhole = 0xE4E2,
    restWhole = 0xE4E3,
    restHalf = 0xE4E4,
    restQuarter = 0xE4E5,
    rest8th = 0xE4E6,
    rest16th = 0xE4E7,
    rest32nd = 0xE4E8,
    rest64th = 0xE4E9,
    rest128th = 0xE4EA
};

// Every glyph above, so a font can be checked for all of them at once. A glyph
// missing from the font would otherwise draw as nothing, and the first anyone
// heard of it would be a gap on a staff.
[[nodiscard]] const std::vector<Glyph>& allGlyphs();

[[nodiscard]] constexpr char32_t codepointOf(Glyph glyph) noexcept
{
    return static_cast<char32_t>(glyph);
}

[[nodiscard]] Glyph noteheadFor(engraving::NoteheadKind head);

// The flag for a stem carrying `flags` of them, pointing up or down. SMuFL
// draws a sixteenth's two hooks as one glyph, not one hook twice, so this is a
// lookup by count rather than a repetition.
//
// Nothing for zero flags, and nothing beyond five: a 256th is legal and the
// layout never produces one, since `NoteValue` stops at the 128th.
[[nodiscard]] std::optional<Glyph> flagFor(int flags, bool stemUp);

[[nodiscard]] Glyph restFor(engraving::WrittenValue value);

// Nothing for `none`, which is a note that draws no accidental.
[[nodiscard]] std::optional<Glyph> accidentalFor(engraving::AccidentalKind kind);

// The clef sign, carrying its octave change where SMuFL has a glyph for it.
//
// `octaveDrawn` reports whether the returned glyph shows the octave change
// itself. A C clef an octave up has no SMuFL glyph, so the plain C clef is
// returned with `octaveDrawn` false and the caller marks the octave some other
// way -- a vocal tenor part drawn as a plain treble clef would claim a register
// it does not sound in.
//
// Nothing for a sign that names no pitch -- percussion, TAB, none -- which the
// layout refuses to place notes under anyway.
struct ClefGlyphChoice
{
    Glyph glyph = Glyph::gClef;
    bool octaveDrawn = true;
};

[[nodiscard]] std::optional<ClefGlyphChoice> clefFor(const score::Clef& clef);

// The digits of one number of a time signature, most significant first. A
// time signature is drawn digit by digit from its own glyphs -- `timeSig1`
// and `timeSig2` side by side for twelve -- never from a text font, whose
// digits are a different shape and a different size.
//
// Nothing for a negative value, which no time signature has.
[[nodiscard]] std::vector<Glyph> timeSignatureDigits(int value);
} // namespace smufl
