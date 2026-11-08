#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "ScoreLayout.h"
#include "platform/score/Score.h"

// Where the practice clock is on a laid-out score, and which notes it is on
// (no JUCE dependency). Every rule the Score View's cursor follows is here, so
// each is tested without a display; see
// openspec/changes/archive/2026-11-08-score-cursor/design.md.
namespace engraving
{
// A place on a laid-out score: which system, and the x on it, in staff spaces
// from the system's left edge.
struct CursorPlace
{
    std::size_t system = 0;
    double x = 0.0;

    bool operator==(const CursorPlace&) const = default;
};

// Where `tick` is drawn (decision 2). The system is the last whose first anchor
// is at or before the tick, so a downbeat that ends one system and opens the
// next is placed at the next. Between two anchors the x is interpolated
// linearly; before a system's first anchor or after its last, it is that
// anchor's. Nothing when no system has an anchor.
[[nodiscard]] std::optional<CursorPlace> placeOfTick(const ScoreLayout& layout, double tick);

// The onset at or before `x` on a system: where a click there starts the clock
// (decision 6). A click left of the first onset is its first. Nothing when the
// system has no anchor.
[[nodiscard]] std::optional<score::Tick> onsetAtOrBefore(const System& system, double x);

// One note the cursor marks: the part it is in, as an index into
// `Score::parts`, and the note within that part.
struct TargetNote
{
    std::size_t part = 0;
    score::NoteRef note;

    bool operator==(const TargetNote&) const = default;
};

struct Targets
{
    // Every note sounding at the tick, a tie's continuation included.
    std::vector<TargetNote> sounding;

    // The notes of the next `lookAhead` onsets after it, across the shown parts,
    // with no tie continuation: those are not new attacks (decision 3).
    std::vector<TargetNote> upcoming;
};

// The targets at `tick` in the parts named by `parts` (indices into
// `Score::parts`). Grace notes and rests are never targets.
[[nodiscard]] Targets targetsAt(
    const score::Score& score,
    const std::vector<std::size_t>& parts,
    double tick,
    int lookAhead);

// The parts a layout draws, as indices into `Score::parts`, in staff order and
// each once.
[[nodiscard]] std::vector<std::size_t> drawnPartsOf(const ScoreLayout& layout);

// The look-ahead, in onsets: none to four, two by default.
inline constexpr int maximumLookAhead = 4;
inline constexpr int defaultLookAhead = 2;
} // namespace engraving
