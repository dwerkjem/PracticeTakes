#pragma once

#include <chrono>
#include <optional>

namespace performance
{
class ApplicationLaunchTimer
{
  public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    explicit ApplicationLaunchTimer(TimePoint processStart = Clock::now()) noexcept;

    void markMainWindowVisible(TimePoint visibleAt = Clock::now()) noexcept;
    [[nodiscard]] std::optional<std::chrono::nanoseconds> duration() const noexcept;

  private:
    TimePoint processStart;
    std::optional<std::chrono::nanoseconds> launchDuration;
};

void markMainWindowVisible() noexcept;
[[nodiscard]] std::optional<std::chrono::nanoseconds> applicationLaunchDuration() noexcept;
} // namespace performance