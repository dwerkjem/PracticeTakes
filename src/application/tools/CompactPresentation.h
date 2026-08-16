#pragma once

#include <algorithm>

// When a tool is too small for its usual display, and which way it should draw
// instead.
//
// One rule, shared, because three tools disagreeing about what "small" means
// would show up as a workspace where the tuner has gone compact and the
// spectrogram beside it has not, at the same pane size. The thresholds are a
// property of the workspace, not of any one tool.
//
// The shape is about which axis is worth drawing along, and a pane is short of
// one dimension or the other rather than of "size". Side-by-side splits make
// panes tall and thin; stacked splits make them short and wide. A trace drawn
// left to right in a pane 180px wide is a smear; the same trace drawn top to
// bottom in the same pane has room.
//
// JUCE-free and header-only, so the rule is unit tested without a display --
// which matters, because the alternative is finding out from a screenshot that
// two tools disagreed.
namespace compact
{
// Below either of these, a full presentation stops being readable rather than
// merely cramped. The width is what the tuner's graph needs once its 48px of
// note labels are taken; the height is what a plot needs before its own axis
// labels start colliding.
inline constexpr int fullWidth = 320;
inline constexpr int fullHeight = 220;

enum class Shape
{
    // Room for the tool's usual display.
    full,

    // Tall and thin: draw along the height.
    vertical,

    // Short and wide: draw along the width.
    horizontal
};

[[nodiscard]] constexpr Shape shapeFor(int width, int height) noexcept
{
    if (width >= fullWidth && height >= fullHeight)
    {
        return Shape::full;
    }

    // Whichever axis has more room is the one to draw along. A square-ish small
    // pane is drawn horizontally, which is the arrangement more tools already
    // have and so the smaller change from their full form.
    return height > width ? Shape::vertical : Shape::horizontal;
}

[[nodiscard]] constexpr bool isCompact(int width, int height) noexcept
{
    return shapeFor(width, height) != Shape::full;
}

// How much detail a compact display should attempt, from 0 (almost none) to 1
// (nearly the full amount), along the axis it is drawn on.
//
// A scale that stays fixed as the space shrinks is how a graph becomes a line:
// six semitones over 90px is not six gridlines, it is a smear with labels on
// it. Callers use this to decide how many gridlines, harmonics, or ticks to
// draw rather than each inventing its own curve.
[[nodiscard]] constexpr float detailFor(int lengthAlongAxis) noexcept
{
    constexpr float leastUsefulLength = 90.0f;
    constexpr float comfortableLength = 420.0f;

    const auto length = static_cast<float>(lengthAlongAxis);
    const auto proportion = (length - leastUsefulLength) / (comfortableLength - leastUsefulLength);

    return std::clamp(proportion, 0.0f, 1.0f);
}
} // namespace compact
