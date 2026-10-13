#pragma once

#include "LayoutTypes.h"

#include "platform/score/Score.h"

// Laying a score out.
//
// The horizontal model is **columns**. For each measure, every distinct
// absolute onset at which any staff of any part has an event becomes a column,
// and every event sits in the column of its own onset. Alignment across staves
// is therefore not a feature that has to be maintained -- it is the data
// structure. "The same tick sits at the same x on both staves of a piano part"
// is true because both staves index the same column list, which is what makes
// it worth building this way rather than laying each staff out alone and
// reconciling afterwards.
//
// A column's width is the larger of two things: a spacing function of the
// musical duration until the next column, and whatever the widest glyph group
// in the column actually needs. The first is why a half note gets more room
// than a quarter; the second is why a column full of accidentals does not
// collide with the next one.
//
// Systems are filled greedily: take measures until the next one would not fit,
// break, repeat. Optimal line breaking is a real improvement and is not this
// pass.
namespace score::engraving
{
struct LayoutOptions
{
    // Available width in staff spaces. A staff space is the distance between
    // two staff lines, so a 1000 px panel drawn at 8 px per space is 125 of
    // them; converting is the caller's business and the only place a pixel
    // exists.
    double availableWidth = 120.0;

    // Horizontal room a quarter note is allotted before glyph widths are taken
    // into account. Everything else scales from it through the spacing
    // function.
    double quarterNoteWidth = 3.5;

    // Vertical gap between the staves of one part -- the inside of a grand
    // staff -- and between one part and the next.
    double staffGapWithinPart = 6.0;
    double gapBetweenParts = 9.0;

    // Vertical gap between one system and the next.
    double systemGap = 10.0;
};

// Lay a score out for one width.
//
// Message thread only, like everything else that touches a Score: the audio
// thread may not read one at all (Score.h), and this allocates freely.
[[nodiscard]] ScoreLayout layOutScore(const Score& score, const LayoutOptions& options);

// The horizontal room a duration is allotted, before glyph widths.
//
// Not proportional to duration, deliberately. Strict proportionality is what
// makes amateur engraving look wrong: a bar of whole notes sprawls while a bar
// of sixteenths becomes unreadable. Real engraving uses a power law, so a half
// note is wider than a quarter but not twice as wide, and this is the simplest
// form of that.
[[nodiscard]] double spacingForDuration(Tick duration, const LayoutOptions& options) noexcept;
} // namespace score::engraving
