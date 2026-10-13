#pragma once

#include "GlyphMetrics.h"

#include "platform/score/ScoreEvent.h"

// What a duration is *written* as: a notehead, a number of flags, a number of
// dots.
//
// This exists because the model does not say. The importer reads MusicXML's
// `<type>` and keeps only ticks, so "is this an eighth note" is a question
// answered by arithmetic rather than by reading a field, and every part of the
// renderer that draws duration -- the notehead, the flag, the beam count, the
// dots -- needs the same answer. One function, so there is one derivation and
// not four that drift.
//
// Carrying the type through the importer would be the better long-term fix and
// belongs to whoever next touches it. It would not remove this: a score may be
// *repaired* on import, so a renderer can never assume a stored type and a
// stored duration still agree.
namespace score::engraving
{
// The shortest value with its own flag glyph and its own rest glyph. Anything
// shorter is drawn as this and reported, rather than drawn as something it is
// not.
inline constexpr int maximumFlags = 4;

struct NoteValue
{
    Glyph notehead = Glyph::noteheadBlack;
    Glyph rest = Glyph::restQuarter;

    // Zero for a quarter or longer, one for an eighth, two for a sixteenth.
    // Also the number of beams a beamed event carries.
    int flags = 0;

    int dots = 0;

    // False when the duration is not any dotted power of two, or is shorter
    // than the shortest value this draws. The caller draws the largest value
    // that fits and raises a diagnostic: a note drawn shorter than it lasts,
    // silently, is the failure mode the renderer's diagnostic rule exists for.
    bool exact = true;
};

// The written value of a duration.
//
// `tuplet` is applied before the decomposition, because a triplet eighth lasts
// two thirds of an eighth and is still *written* as an eighth. Skipping that
// draws every triplet in every score as a dotted something, which looks like a
// font problem rather than an arithmetic one.
[[nodiscard]] NoteValue noteValueFor(Tick duration, const TupletRatio& tuplet = {}) noexcept;
} // namespace score::engraving
