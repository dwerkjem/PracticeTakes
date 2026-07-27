#include "features/performance/ApplicationLaunchTimer.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("application launch timer records time until the main window first appears")
{
    using namespace std::chrono_literals;
    const performance::ApplicationLaunchTimer::TimePoint processStart{};
    performance::ApplicationLaunchTimer timer(processStart);

    REQUIRE_FALSE(timer.duration().has_value());
    timer.markMainWindowVisible(processStart + 125ms);

    REQUIRE(timer.duration() == 125ms);
}

TEST_CASE("application launch timer preserves the first visible timestamp")
{
    using namespace std::chrono_literals;
    const performance::ApplicationLaunchTimer::TimePoint processStart{};
    performance::ApplicationLaunchTimer timer(processStart);

    timer.markMainWindowVisible(processStart + 125ms);
    timer.markMainWindowVisible(processStart + 250ms);

    REQUIRE(timer.duration() == 125ms);
}