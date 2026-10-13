#pragma once

#include <optional>
#include <vector>

#include "LayoutTypes.h"

#include "platform/score/ScoreAttributes.h"

// What stands at the left edge of a staff before any music: the clef, then the
// key signature, then the time signature, in that order and each at its own
// width.
//
// Split out of `ScoreLayout.cpp` because none of it depends on columns, system
// breaking, or the available width -- it is a function of three attributes and
// a clef -- and because the vertical placement of a key signature is the one
// part of this layer that is pure convention rather than arithmetic. A table
// that says "the third sharp sits in the space above the top line of a treble
// staff" is worth asserting directly rather than through a laid-out score.
//
// No JUCE and no font, like the rest of the engraving layer. Positions are in
// staff spaces from the left edge of the head, and steps are as
// `StaffPosition.h` defines them.
namespace score::engraving
{
// The glyph a clef sign draws with. Anything that is not G, F or C -- a
// percussion or TAB staff -- has no pitched meaning, and `hasPitchedStaff` is
// how the caller knows not to draw a key signature on it.
struct ClefGlyph
{
    Glyph glyph = Glyph::gClef;

    // Steps from the bottom line to the line the sign centres on.
    int staffStep = 2;

    bool hasPitchedStaff = true;
};

[[nodiscard]] ClefGlyph clefGlyphFor(const Clef& clef) noexcept;

// The staff steps a key signature's accidentals occupy under a clef, in the
// order they are written.
//
// Empty for `fifths == 0`, and empty for a staff with no pitched reading.
// `fifths` beyond seven in either direction is clamped: the model keeps what
// the file said, and a file saying nine sharps is wrong rather than
// interesting.
[[nodiscard]] std::vector<PlacedGlyph>
keySignatureGlyphs(const KeySignature& key, const Clef& clef);

// The digits of a time signature, numerator above denominator.
//
// Each number is centred on the other, so 12/8 does not leave the 8 hanging
// under the 1.
[[nodiscard]] std::vector<PlacedGlyph> timeSignatureGlyphs(const TimeSignature& time);

// One staff's head: everything to draw, and how much room it takes.
struct SystemHead
{
    std::vector<PlacedGlyph> glyphs;

    // Staff spaces from the left edge of the system to where music may start.
    double width = 0.0;
};

// Lay out one staff's head. `key` and `time` are what is in force at the first
// measure of the system, not what that measure changes -- a system starting
// mid-piece still shows both, because a reader who scrolls to it has not seen
// the first bar.
[[nodiscard]] SystemHead systemHeadFor(
    const Clef& clef,
    const std::optional<KeySignature>& key,
    const std::optional<TimeSignature>& time);
} // namespace score::engraving
