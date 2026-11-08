#pragma once

#include <cmath>
#include <cstddef>
#include <optional>

#include "ScoreLayout.h"
#include "StaffGeometry.h"

// Which notehead of a laid-out system is at a point (no JUCE dependency), for
// the Score View's right-click menu
// (openspec/changes/target-note-picker/design.md, decision 6).
namespace engraving
{
// `x` and `y` in staff spaces from the system's origin: the left edge, and the
// top line of its first staff. The nearest notehead within about a head's
// width and height, or nothing. Grace notes count; a click is a click.
[[nodiscard]] inline std::optional<std::size_t> noteheadAt(const System& system, double x, double y)
{
    constexpr double reachAcross = 1.2;
    constexpr double reachUp = 0.8;
    const double middle = LayoutOptions{}.noteheadWidth / 2.0;

    std::optional<std::size_t> nearest;
    double best = 0.0;
    for (std::size_t index = 0; index < system.noteheads.size(); ++index)
    {
        const NoteheadGlyph& head = system.noteheads[index];
        const auto staff = static_cast<std::size_t>(head.staff < 0 ? 0 : head.staff);
        const double offset = staff < system.staffOffsets.size() ? system.staffOffsets[staff] : 0.0;
        const double across = std::abs(x - (head.x + middle));
        const double up = std::abs(y - (offset + spacesBelowTopLine(head.position)));
        if (across > reachAcross || up > reachUp)
        {
            continue;
        }
        const double distance = (across * across) + (up * up);
        if (!nearest.has_value() || distance < best)
        {
            nearest = index;
            best = distance;
        }
    }
    return nearest;
}
} // namespace engraving
