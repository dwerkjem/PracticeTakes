#pragma once

#include <cmath>
#include <string>

#include "Take.h"

// What the transport says about a take, in a short phrase (no JUCE
// dependency, so every wording is tested).
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

// A live take: "Counting in", "Recording 0:12", "Paused at 0:12".
// An ended one: "Take 3 complete, 1:05", "Take 3 stopped in bar 14, 0:42",
// "Take 3 discarded", "Take 3 failed: another score was opened".
[[nodiscard]] inline std::string describe(const Take& take)
{
    const std::string length = formatDuration(take.performedSeconds());
    const std::string name = "Take " + std::to_string(take.id);

    switch (take.state())
    {
    case TakeState::ready:
        return "Starting";
    case TakeState::countingIn:
        return "Counting in";
    case TakeState::performing:
        return "Recording " + length;
    case TakeState::paused:
        return "Paused at " + length;
    case TakeState::completed:
        return name + " complete, " + length;
    case TakeState::stoppedEarly:
        return take.stoppedInBar.has_value()
                   ? name + " stopped in bar " + std::to_string(*take.stoppedInBar + 1) + ", " +
                         length
                   : name + " stopped early, " + length;
    case TakeState::cancelled:
        return name + " discarded";
    case TakeState::failed:
        return name + " failed: " + take.lifecycle.failureReason();
    }
    return {};
}
} // namespace take
