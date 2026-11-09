#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "../take/Take.h"
#include "NoteMatching.h"

// What a review shows of a judged take: the part it is judged against, and one
// row per result in time order (no JUCE dependency). The words for a row and
// for the summary are the application's: l10n::reviewRow, l10n::reviewSummary.
// openspec/changes/archive/2026-11-08-take-review/design.md, decisions 2 and 4.
namespace evaluation
{
// One line of the Take Review tool's list.
struct ReviewRow
{
    // Where it happened: the score tick the clock seeks to, and the take's
    // frame, corrected for latency for an extra note.
    double tick = 0.0;
    std::int64_t frame = 0;

    // Zero-based, as the clock counts them.
    std::int64_t bar = 0;
    int beat = 0;

    // The result in the report this row shows: one of the two is set.
    std::optional<std::size_t> result;
    std::optional<std::size_t> extra;

    // Wrong, missed, extra, early or late: what a reader looks for.
    bool mistake = false;
};

// A part, and the take judged against it.
struct ReviewedPart
{
    std::size_t part = 0;
    NoteReport report;
};

// The name of a key: sharps, scientific octave ("C#4"). A played key has no
// spelling, so this is not `score::spelledName`.
[[nodiscard]] std::string keyName(int midi);

// The rows of `report`, judged from `take`, in time order, extras included.
[[nodiscard]] std::vector<ReviewRow> reviewRows(const take::Take& take, const NoteReport& report);

// The sounding pitch a result was written as, spelled as the file spelled it.
[[nodiscard]] std::string writtenNameOf(const take::Take& take, const NoteResult& result);

// The on-time and early/late moments of a report: a chord's notes share one.
struct MomentCounts
{
    std::size_t early = 0;
    std::size_t onTime = 0;
    std::size_t late = 0;
};

[[nodiscard]] MomentCounts momentsOf(const NoteReport& report);

// How well a take matches a part: twice its correct notes, plus its on-time
// moments (decision 2).
[[nodiscard]] std::size_t matchScore(const NoteReport& report);

// The take judged against every part of its score, and the part it matches
// best; ties go to the earlier part. Part 0, unjudged, for a take with no score.
[[nodiscard]] ReviewedPart bestPartFor(
    const take::Take& take,
    const TimingSettings& timing = {},
    const IntonationSettings& intonation = {});
} // namespace evaluation
