#pragma once

#include "../../application/tools/ToolSettingsPayload.h"
#include "engraving/ScoreCursor.h"
#include "engraving/ScoreLayout.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

// What a Score View remembers about itself, and how it is saved (no JUCE
// dependency). See decisions 4 and 5 of
// openspec/changes/archive/2026-11-02-score-view-modes/design.md.
namespace score_view
{
// How the laid-out systems are arranged on screen.
enum class ViewMode
{
    // Fixed-size pages, one at a time. The default for a new Score View.
    page,

    // One column of systems, scrolled up and down: the view as it always was.
    vertical,

    // Every bar on one system, scrolled left to right.
    horizontal
};

// How Page view sizes its page (score-cursor design.md decision 7).
enum class PageFit
{
    // As wide as the panel, scrolled within when taller: the default, and how
    // Page view always worked.
    width,

    // As tall as the panel, so the whole page shows at once, smaller.
    page
};

// The staff sizes enlarge and shrink step through, as pixels per staff space,
// smallest first. The old Small, Medium and Large (5.2, 7.0, 9.4) are on it, so
// nobody's chosen size disappears.
inline constexpr std::array<double, 9> sizeLadder{4.0, 4.6, 5.2, 6.0, 7.0, 8.2, 9.4, 11.0, 13.0};

// Index of 7.0 -- the old Medium, and the size a new Score View starts at.
inline constexpr std::size_t defaultSizeStep = 4;

struct Settings
{
    ViewMode view = ViewMode::page;
    std::size_t sizeStep = defaultSizeStep;

    // The parts this Score View does not draw, by id and name together (see
    // openspec/changes/archive/2026-11-06-score-part-selection/design.md decisions 1 and 2).
    // Empty, the default, draws every part.
    std::vector<engraving::PartKey> hiddenParts;

    // How many onsets ahead of the practice clock to ring, none to four
    // (score-cursor decision 3).
    int lookAhead = engraving::defaultLookAhead;

    // Whether the view follows the clock while it plays (decision 5).
    bool follow = true;

    PageFit pageFit = PageFit::width;

    bool operator==(const Settings&) const = default;
};

// The ladder step nearest to `pixelsPerSpace`, so a size saved by a version
// with a different ladder still comes back as something offered.
[[nodiscard]] std::size_t nearestSizeStep(double pixelsPerSpace);

[[nodiscard]] std::string_view nameOf(ViewMode view);
[[nodiscard]] std::optional<ViewMode> viewNamed(std::string_view name);

// The payload version, which the Score View's ToolDefinition::settingsVersion
// must equal.
inline constexpr int settingsVersion = 1;

// "view=page;size=7" -- the view by name and the size in pixels per space --
// then, when any part is hidden, ";hidden=P1:Voice,P3:Alto"; then, each only
// when it is not the default, ";ahead=3", ";follow=0" and ";fit=page". A part's id is an
// XML name and never holds `:` `,` `;` or `=`; its name can, so the name is
// percent-encoded for those and `%`. A version without hidden parts ignores the
// key, which is why adding it did not need a new `settingsVersion`.
[[nodiscard]] ToolSettingsPayload encode(const Settings& settings);

// Anything the payload does not say, or says in a way that cannot be read,
// falls back to its default rather than discarding the rest: a view saved
// beside a size that no longer parses still comes back as that view.
[[nodiscard]] Settings decode(const ToolSettingsPayload& payload);
} // namespace score_view
