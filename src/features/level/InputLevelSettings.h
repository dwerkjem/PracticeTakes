#pragma once

#include "../../application/tools/ToolSettingsPayload.h"

#include <string>

// The Input Level tool's settings: which way its bar runs (no JUCE
// dependency, so the format is tested without a display).
namespace input_level
{
// The catalog's settingsVersion for "input-level"; a test pins the two.
inline constexpr int settingsVersion = 1;

enum class Orientation
{
    // Upright in a tall, thin pane, across in any other: the compact rule
    // every tool shares.
    automatic,
    horizontal,
    vertical
};

[[nodiscard]] inline ToolSettingsPayload encode(Orientation orientation)
{
    const char* name = "auto";
    if (orientation == Orientation::horizontal)
    {
        name = "horizontal";
    }
    else if (orientation == Orientation::vertical)
    {
        name = "vertical";
    }
    return {settingsVersion, std::string("orientation=") + name};
}

// What was saved, or automatic for anything else: another version, an
// unknown value, nothing at all.
[[nodiscard]] inline Orientation decode(const ToolSettingsPayload& payload)
{
    if (payload.version != settingsVersion)
    {
        return Orientation::automatic;
    }
    if (payload.data == "orientation=horizontal")
    {
        return Orientation::horizontal;
    }
    if (payload.data == "orientation=vertical")
    {
        return Orientation::vertical;
    }
    return Orientation::automatic;
}

// Whether the bar runs upright, given the choice and whether the pane is the
// tall, thin shape the automatic choice turns upright for.
[[nodiscard]] constexpr bool drawsVertically(Orientation orientation, bool tallAndThin) noexcept
{
    return orientation == Orientation::automatic ? tallAndThin
                                                 : orientation == Orientation::vertical;
}
} // namespace input_level
