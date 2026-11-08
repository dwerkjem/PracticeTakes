#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "../../../platform/evaluation/NoteMatching.h"
#include "ScoreLayout.h"

// Where a take review's marks go on a laid-out score (no JUCE dependency).
// openspec/changes/take-review/design.md, decision 3.
//
// A judged note is found by its part and NoteRef, exactly as the cursor finds
// what it marks, so its mark sits on its own notehead. An extra note was never
// written, so it goes at its tick, above its system's top staff.
namespace engraving
{
enum class PitchMark
{
    // A microphone onset, whose pitch is not judged: only its timing is shown.
    none,
    correct,
    wrong,
    missed
};

enum class TimingMark
{
    none,
    early,
    late
};

struct NoteMark
{
    std::size_t system = 0;

    // Into that system's `noteheads`.
    std::size_t notehead = 0;

    PitchMark pitch = PitchMark::none;
    TimingMark timing = TimingMark::none;

    // What was played instead, for a wrong note.
    std::optional<int> playedMidi;
};

struct ExtraMark
{
    std::size_t system = 0;
    double x = 0.0;

    // Unknown for a microphone onset.
    std::optional<int> midi;
};

struct ReviewMarks
{
    std::vector<NoteMark> notes;
    std::vector<ExtraMark> extras;
};

// The marks for `report` on `layout`. `extraTicks` is each of the report's
// extra notes' tick, in the same order (evaluation::reviewRows has them); one
// left out is not drawn, and one past the last note is at the last anchor, as
// the cursor would be. A note whose part is
// hidden, or a tie's continuation, has no notehead to mark and is skipped.
[[nodiscard]] ReviewMarks reviewMarksFor(
    const ScoreLayout& layout,
    const evaluation::NoteReport& report,
    const std::vector<double>& extraTicks);
} // namespace engraving
