#pragma once

#include <juce_graphics/juce_graphics.h>

#include "TextScale.h"

#include <cmath>

// The JUCE side of the text size: a role's font, and a height that grows with
// the text (global-text-size decisions 2 and 4). Text a component paints
// itself asks for its role here; a juce::Label is given its role's base size
// (textFont at 100) and the look-and-feel scales it.
namespace appearance
{
[[nodiscard]] inline juce::FontOptions
textFont(TextRole role, int percent, int styleFlags = juce::Font::plain)
{
    return juce::FontOptions(textSize(role, percent), styleFlags);
}

// A role's font at its base size, for a juce::Label, which the look-and-feel
// scales.
[[nodiscard]] inline juce::FontOptions labelFont(TextRole role, int styleFlags = juce::Font::plain)
{
    return juce::FontOptions(baseSize(role), styleFlags);
}

// A height laid out for text at 100 percent -- a row, a button, a strip --
// grown in step with the text. Never smaller than it is at 100: a smaller text
// size leaves the layout as it was, so nothing gets cramped.
[[nodiscard]] inline int scaledHeight(int base, int percent) noexcept
{
    return percent <= TextScale::defaultPercent
               ? base
               : static_cast<int>(
                     std::lround(static_cast<double>(base) * TextScale::factor(percent)));
}
} // namespace appearance
