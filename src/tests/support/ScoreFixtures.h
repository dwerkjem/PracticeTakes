#pragma once

#include <initializer_list>
#include <optional>
#include <vector>

#include "platform/score/ScoreEvent.h"

// Small conveniences for building score-model values by hand.
//
// The model is value types plus a builder precisely so that a test can assemble
// a score without a parser, which is what lets the invariants be tested before
// any XML is read. These just keep that assembly readable.
namespace testing::score
{
// An event's notes from bare pitches, for the tests that do not care about tie
// linkage or transposition. Written and sounding pitch are set the same, which
// is what every non-transposing part looks like. Tests that care about either
// set the fields afterwards, because both are per note and writing them inline
// would hide that.
[[nodiscard]] inline std::vector<::score::Note>
notesOf(std::initializer_list<::score::Pitch> pitches)
{
    std::vector<::score::Note> notes;
    notes.reserve(pitches.size());

    for (const ::score::Pitch& pitch : pitches)
    {
        notes.push_back(::score::Note{pitch, pitch, std::nullopt, std::nullopt});
    }

    return notes;
}
} // namespace testing::score
