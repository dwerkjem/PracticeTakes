#pragma once

#include <cstddef>
#include <limits>
#include <vector>

#include "ScoreLayout.h"

// Cutting laid-out systems into pages (no JUCE dependency).
//
// The layout engine knows nothing about pages: it lays systems out at a width
// and stacks them. A page view then cuts that stack between systems, which
// keeps every engraving rule -- and every test of one -- the same in every view.
// See decisions 1 and 2 of
// openspec/changes/archive/2026-11-02-score-view-modes/design.md.
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

    // The one system on this page is larger than a page -- taller than its
    // height, or a bar wider than its width -- so the page has to be drawn
    // smaller to show all of it. Such a system always has a page to itself, so
    // shrinking it shrinks nothing else.
    bool oversize = false;
};

// Greedy and in order: each page takes whole systems until the next would not
// fit, and a system never spans two pages. A system larger than a page in
// either direction -- taller than `pageHeight`, or wider than `pageWidth`
// because one bar would not fit the width at all -- goes on a page of its own,
// marked `oversize`, and the page after it starts afresh. No systems, no pages.
[[nodiscard]] std::vector<Page> paginate(
    const std::vector<System>& systems,
    double pageHeight,
    double pageWidth = std::numeric_limits<double>::infinity());

// How much a page has to be scaled down for every system on it to show whole:
// 1 for a page whose systems fit, less for an oversize one. Never more than 1
// -- a page is never enlarged to fill space.
[[nodiscard]] double
fitScale(const Page& page, const std::vector<System>& systems, double pageHeight, double pageWidth);
} // namespace engraving
