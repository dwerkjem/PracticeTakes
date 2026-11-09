#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "platform/transport/PlayheadSmoother.h"

#include <cmath>
#include <vector>

using Catch::Approx;
using transport::PlayheadSmoother;

namespace
{
// A clock at 960 ticks a second whose reports arrive at 60 Hz but whose
// position moves in blocks of 512 frames at 48 kHz: what the score cursor is
// drawn from.
[[nodiscard]] double blockedTick(double seconds)
{
    constexpr double blockSeconds = 512.0 / 48000.0;
    return std::floor(seconds / blockSeconds) * blockSeconds * 960.0;
}
} // namespace

TEST_CASE("a stopped clock is drawn exactly where it is", "[transport][playhead]")
{
    PlayheadSmoother smoother;
    smoother.observe(480.0, 1.0, false);
    CHECK(smoother.at(1.0) == 480.0);
    CHECK(smoother.at(5.0) == 480.0);
    CHECK_FALSE(smoother.isRunning());
}

TEST_CASE("between reports the playhead moves evenly", "[transport][playhead]")
{
    PlayheadSmoother smoother;
    for (int report = 0; report <= 120; ++report)
    {
        const double now = report / 60.0;
        smoother.observe(blockedTick(now), now, true);
    }

    // Drawn at 120 Hz over the next reports: every step the same size, where
    // the reports themselves step by one block or two.
    std::vector<double> steps;
    double previous = smoother.at(2.0);
    for (int frame = 1; frame <= 20; ++frame)
    {
        const double now = 2.0 + (frame / 120.0);
        if (frame % 2 == 0)
        {
            smoother.observe(blockedTick(now), now, true);
        }
        const double drawn = smoother.at(now);
        steps.push_back(drawn - previous);
        previous = drawn;
    }
    for (const double step : steps)
    {
        CHECK(step == Approx(8.0).margin(2.5));
    }

    // And it stays with the clock rather than drifting from it.
    CHECK(
        smoother.at(2.0 + (20.0 / 120.0)) ==
        Approx(blockedTick(2.0 + (20.0 / 120.0))).margin(12.0));
}

TEST_CASE("a jump in the clock is drawn as a jump", "[transport][playhead]")
{
    PlayheadSmoother smoother;
    for (int report = 0; report <= 60; ++report)
    {
        smoother.observe(report * 16.0, report / 60.0, true);
    }

    // A seek back to the start, and a loop's wrap: both taken as they are.
    smoother.observe(0.0, 61.0 / 60.0, true);
    CHECK(smoother.at(61.0 / 60.0) == 0.0);

    smoother.observe(5000.0, 62.0 / 60.0, true);
    CHECK(smoother.at(62.0 / 60.0) == 5000.0);
}

TEST_CASE("a playhead that stops hearing from the clock does not run away", "[transport][playhead]")
{
    PlayheadSmoother smoother;
    for (int report = 0; report <= 60; ++report)
    {
        smoother.observe(report * 16.0, report / 60.0, true);
    }
    const double last = smoother.at(1.0);
    CHECK(smoother.at(10.0) <= last + (960.0 * 0.2) + 1.0);
}
