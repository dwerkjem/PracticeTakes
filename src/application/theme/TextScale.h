#pragma once

#include <array>
#include <cmath>
#include <string_view>

// One text size for the whole application (openspec/changes/global-text-size),
// with no JUCE dependency so the rule is tested without a display.
//
// The size is a scale, not a point size: the application uses many sizes on
// purpose, and a scale keeps their proportions (design.md decision 1). Each
// kind of text is a role with a base size (decision 2); a call site asks for
// its role, never for a number, so every size the application uses has a name
// and appears in the Settings preview.
namespace appearance
{
// The text size, as a whole percentage of each role's base size.
struct TextScale
{
    static constexpr int minimumPercent = 80;
    static constexpr int maximumPercent = 150;
    static constexpr int stepPercent = 10;
    static constexpr int defaultPercent = 100;

    // The nearest step within the bounds; anything that is not a number is
    // the default. A stored value goes through here, so a hand-edited or
    // out-of-range one still loads as a size the setting can show.
    [[nodiscard]] static int snapped(double percent) noexcept
    {
        if (!std::isfinite(percent))
        {
            return defaultPercent;
        }
        const double clamped = std::fmin(
            std::fmax(percent, static_cast<double>(minimumPercent)),
            static_cast<double>(maximumPercent));
        const double steps = std::round(
            (clamped - static_cast<double>(minimumPercent)) / static_cast<double>(stepPercent));
        return minimumPercent + (static_cast<int>(steps) * stepPercent);
    }

    // `base` points at `percent`, rounded to half a point so text does not
    // shimmer between sizes. At 100% it is `base` itself, for every base the
    // role table uses.
    [[nodiscard]] static float scaled(float base, int percent) noexcept
    {
        const double size = static_cast<double>(base) * static_cast<double>(percent) / 100.0;
        return static_cast<float>(std::round(size * 2.0) / 2.0);
    }

    // The factor for a height that follows the text, such as a row's.
    [[nodiscard]] static constexpr double factor(int percent) noexcept
    {
        return static_cast<double>(percent) / 100.0;
    }
};

// Every kind of text the application draws at a size of its own choosing.
//
// The base sizes are exactly today's (2026-11-09), so 100% changes nothing:
// one role for each size in use, named for what is drawn at it, rather than
// six broad roles that would have moved text by a point or two at 100%
// (decision 2, as recorded in design.md). Text that sizes itself to its pane
// -- the tuner's note name and signs, notation, Now & Next's note -- is not a
// role and does not scale.
enum class TextRole
{
    tiny,
    fine,
    axis,
    caption,
    small,
    body,
    control,
    prompt,
    label,
    button,
    subheading,
    menu,
    heading,
    title,
    panelHeading,
    readout,
    display
};

struct TextRoleInfo
{
    TextRole role;

    // What the Settings preview calls it.
    std::string_view name;

    // The base size, in points, at 100%.
    float baseSize;
};

inline constexpr std::array<TextRoleInfo, 17> textRoles{{
    {TextRole::tiny, "Tiny labels in a small pane", 9.0f},
    {TextRole::fine, "Key names", 10.0f},
    {TextRole::axis, "Axis and tick labels", 11.0f},
    {TextRole::caption, "Notes under the score", 11.5f},
    {TextRole::small, "Small print", 12.0f},
    {TextRole::body, "Body text", 13.0f},
    {TextRole::control, "Lists and readouts", 14.0f},
    {TextRole::prompt, "Prompts", 14.5f},
    {TextRole::label, "Labels and status", 15.0f},
    {TextRole::button, "Buttons and choices", 16.0f},
    {TextRole::subheading, "Subheadings", 16.0f},
    {TextRole::menu, "Menus and messages", 17.0f},
    {TextRole::heading, "Tool headings", 18.0f},
    {TextRole::title, "Titles", 20.0f},
    {TextRole::panelHeading, "Settings headings", 22.0f},
    {TextRole::readout, "Large readouts", 30.0f},
    {TextRole::display, "The level", 34.0f},
}};

[[nodiscard]] constexpr const TextRoleInfo& roleInfo(TextRole role) noexcept
{
    for (const TextRoleInfo& info : textRoles)
    {
        if (info.role == role)
        {
            return info;
        }
    }
    return textRoles.front();
}

[[nodiscard]] constexpr float baseSize(TextRole role) noexcept
{
    return roleInfo(role).baseSize;
}

// `role`'s size at `percent`.
[[nodiscard]] inline float textSize(TextRole role, int percent) noexcept
{
    return TextScale::scaled(baseSize(role), percent);
}
} // namespace appearance
