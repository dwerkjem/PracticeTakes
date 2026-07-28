#include "ApplicationLaunchTimer.h"

namespace performance
{
namespace
{
ApplicationLaunchTimer launchTimer;
}

ApplicationLaunchTimer::ApplicationLaunchTimer(TimePoint processStartIn) noexcept
    : processStart(processStartIn)
{
}

void ApplicationLaunchTimer::markMainWindowVisible(TimePoint visibleAt) noexcept
{
    if (!launchDuration)
    {
        launchDuration = visibleAt - processStart;
    }
}

std::optional<std::chrono::nanoseconds> ApplicationLaunchTimer::duration() const noexcept
{
    return launchDuration;
}

void markMainWindowVisible() noexcept
{
    launchTimer.markMainWindowVisible();
}

std::optional<std::chrono::nanoseconds> applicationLaunchDuration() noexcept
{
    return launchTimer.duration();
}
} // namespace performance