#pragma once

#include <cstddef>
#include <vector>

#include "ScoreLayout.h"

// Cutting laid-out systems into pages (no JUCE dependency).
//
// The layout engine knows nothing about pages: it lays systems out at a width
// and stacks them. A page view then cuts that stack between systems, which
// keeps every engraving rule -- and every test of one -- the same in every view.
// See openspec/changes/score-view-modes/design.md, decisions 1 and 2.
namespace engraving
{
struct Page
{
    // The systems on this page, as indices into `ScoreLayout::systems`,
    // inclusive.
    std::size_t firstSystem = 0;
    std::size_t lastSystem = 0;

    // Where the page's content starts, in staff spaces from the top of the
    // layout: the band top of its first system. Drawing a page shifts its
    // systems up by this much.
    double top = 0.0;

    // The one system on this page is taller than a page, so its foot is cut
    // off. Said, not hidden: the view reports it rather than shrinking the
    // music.
    bool overflows = false;
};

// Greedy and in order: each page takes whole systems until the next would not
// fit, and a system never spans two pages. A system taller than a page gets a
// page to itself, marked `overflows`. No systems, no pages. A page height that
// is not positive puts every system on its own page.
[[nodiscard]] std::vector<Page> paginate(const std::vector<System>& systems, double pageHeight);
} // namespace engraving
