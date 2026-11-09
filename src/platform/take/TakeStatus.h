#pragma once

#include <cmath>
#include <string>

#include "Take.h"

// How long a take ran, as the transport shows it (no JUCE dependency). The
// words around it are l10n::takeStatus's.
namespace take
{
// "1:05": minutes and seconds, rounded down.
[[nodiscard]] inline std::string formatDuration(double seconds)
{
    const auto whole = static_cast<long long>(std::floor(std::max(0.0, seconds)));
    const long long minutes = whole / 60;
    const long long rest = whole % 60;
    return std::to_string(minutes) + ":" + (rest < 10 ? "0" : "") + std::to_string(rest);
}

} // namespace take
