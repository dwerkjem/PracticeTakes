#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "../../platform/score/PartTimeline.h"

// What Now & Next shows at a tick: the item sounding, the items after it, and
// how far through the current item the listener is (no JUCE dependency).
// openspec/changes/archive/2026-11-09-now-and-next/design.md, decisions 2 and 3.
namespace nownext
{
inline constexpr std::size_t defaultLookAhead = 4;
inline constexpr std::size_t lookAheadChoices[] = {2, 4, 6, 8};

struct View
{
    // The item sounding, if any.
    std::optional<std::size_t> now;

    // The items after it, or after the tick when nothing sounds (before the
    // first note, during a count-in), up to the look-ahead.
    std::vector<std::size_t> next;

    // 0-1 through `now`; 0 without one.
    double through = 0.0;
};

// A count of beats as a musician says it: whole beats and a common fraction
// ("1 1/2 beats" written with the fraction glyph), to the nearest
// twenty-fourth; a decimal for anything finer.
[[nodiscard]] inline std::string beatsText(double beats)
{
    struct Fraction
    {
        long twentyFourths;
        const char* glyph;
    };
    static constexpr std::array<Fraction, 11> fractions{{
        {3, "\xE2\x85\x9B"},
        {4, "\xE2\x85\x99"},
        {6, "\xC2\xBC"},
        {8, "\xE2\x85\x93"},
        {9, "\xE2\x85\x9C"},
        {12, "\xC2\xBD"},
        {15, "\xE2\x85\x9D"},
        {16, "\xE2\x85\x94"},
        {18, "\xC2\xBE"},
        {20, "\xE2\x85\x9A"},
        {21, "\xE2\x85\x9E"},
    }};
    const auto decimal = [beats]
    {
        const long tenths = std::lround(beats * 10.0);
        return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
    };
    const double scaled = beats * 24.0;
    const long rounded = std::lround(scaled);
    std::string text;
    if (std::abs(scaled - static_cast<double>(rounded)) > 0.05 || rounded <= 0)
    {
        text = decimal();
    }
    else
    {
        const long whole = rounded / 24;
        const long part = rounded % 24;
        if (whole > 0 || part == 0)
        {
            text = std::to_string(whole);
        }
        if (part != 0)
        {
            const auto* found = std::find_if(
                fractions.begin(), fractions.end(),
                [part](const Fraction& fraction) { return fraction.twentyFourths == part; });
            text = found == fractions.end() ? decimal() : text + found->glyph;
        }
    }
    return text + (beats <= 1.0 + 1e-6 ? " beat" : " beats");
}

[[nodiscard]] inline View
viewAt(const score::PartTimeline& timeline, double tick, std::size_t lookAhead)
{
    View view;
    view.now = score::itemAt(timeline, tick);
    std::optional<std::size_t> first =
        view.now.has_value() ? std::optional<std::size_t>(*view.now + 1)
                             : score::itemAfter(timeline, tick);
    if (first.has_value())
    {
        for (std::size_t index = *first; index < timeline.size() && view.next.size() < lookAhead;
             ++index)
        {
            view.next.push_back(index);
        }
    }
    if (view.now.has_value())
    {
        const score::TimelineItem& item = timeline[*view.now];
        const double length = static_cast<double>(item.end - item.start);
        view.through =
            length > 0.0 ? std::clamp((tick - static_cast<double>(item.start)) / length, 0.0, 1.0)
                         : 0.0;
    }
    return view;
}
} // namespace nownext
