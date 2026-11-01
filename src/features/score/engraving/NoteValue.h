#pragma once

#include <optional>
#include <string>

#include "platform/score/MusicalTime.h"
#include "platform/score/ScoreEvent.h"

// Model ticks to the written value a reader sees (no JUCE dependency).
//
// The model stores duration as an integer count of ticks, 3840 to the quarter
// note, and says nothing about how it is written -- invariant 10 keeps notehead
// shapes and flag counts out of it. Deriving one from the other is this file's
// whole job.
namespace engraving
{
// The shape of the notehead, which is what distinguishes the long values from
// each other and from everything a stem is attached to.
enum class NoteheadKind
{
    // A double whole note: an open head with vertical strokes either side. Rare
    // but present in early repertoire, which the test corpus has plenty of.
    breve,

    whole,
    half,

    // Everything from a quarter note down. The flag count is what tells them
    // apart, not the head.
    filled
};

// A written note value, independent of dots.
enum class WrittenValue
{
    breve,
    whole,
    half,
    quarter,
    eighth,
    sixteenth,
    thirtySecond,
    sixtyFourth,
    hundredTwentyEighth
};

// A value plus its augmentation dots -- the pair that a duration converts to,
// and the pair a glyph is drawn from.
struct WrittenNoteValue
{
    WrittenValue value = WrittenValue::quarter;

    // Zero, one or two. Three is legal notation and vanishingly rare; a
    // triple-dotted duration is reported as unnotatable rather than drawn with
    // two dots, because two dots is a different duration.
    int dots = 0;
};

// Ticks the value spans, undotted.
[[nodiscard]] score::Tick ticksFor(WrittenValue value);

// Ticks the value spans with its dots: one dot is half again, two dots three
// quarters again.
[[nodiscard]] score::Tick ticksFor(const WrittenNoteValue& value);

[[nodiscard]] NoteheadKind noteheadFor(WrittenValue value);

// Flags on the stem: none for a quarter and longer, one for an eighth, and one
// more for each halving after that. Rests use the same count.
[[nodiscard]] int flagCountFor(WrittenValue value);

// Whether the value is drawn with a stem at all. A breve and a whole note are
// not.
[[nodiscard]] bool hasStem(WrittenValue value);

// How to name the value in a notice or a diagnostic.
[[nodiscard]] std::string nameOf(WrittenValue value);

// The written value a duration corresponds to, or nothing when none does.
//
// `tuplet` is applied first: a triplet eighth lasts 1280 ticks and is *written*
// as an eighth, so the ratio is multiplied back out before matching. A ratio
// that does not divide the duration exactly means the two disagree, and that is
// reported rather than rounded -- see the note on `exact` in MusicalTime.h for
// the same principle one layer down.
//
// Returns nothing for a zero duration, which in this model means a grace note
// (invariant 3): a grace note's written value is a convention rather than an
// arithmetic fact, so the caller chooses it.
[[nodiscard]] std::optional<WrittenNoteValue>
writtenValueFor(score::Tick duration, const score::TupletRatio& tuplet);

// The same question for an event, reading its duration and its tuplet ratio.
[[nodiscard]] std::optional<WrittenNoteValue> writtenValueFor(const score::ScoreEvent& event);
} // namespace engraving
