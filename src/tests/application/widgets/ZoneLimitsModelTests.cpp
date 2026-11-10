#include <catch2/catch_test_macros.hpp>

#include "application/widgets/ZoneLimitsModel.h"

#include <cmath>

namespace
{
// Pitch's limits: near perfect 1-20, in tune 2-49, out of tune 3-50, a cent apart.
const zone_limits::Model pitchModel{
    {zone_limits::Limit{1.0, 20.0}, zone_limits::Limit{2.0, 49.0}, zone_limits::Limit{3.0, 50.0}},
    1.0,
    1.0};
} // namespace

TEST_CASE("a dragged limit stays in its own range", "[widgets][zone-limits]")
{
    const std::array<double, 3> start{5.0, 25.0, 40.0};
    CHECK(zone_limits::moved(pitchModel, start, 0, -10.0)[0] == 1.0);
    CHECK(zone_limits::moved(pitchModel, start, 2, 99.0)[2] == 50.0);
    // On the step.
    CHECK(zone_limits::moved(pitchModel, start, 1, 30.4)[1] == 30.0);
}

TEST_CASE("a limit dragged into its neighbour pushes it along", "[widgets][zone-limits]")
{
    const std::array<double, 3> start{5.0, 25.0, 40.0};
    // In tune up to 45 pushes out of tune to 46, a cent beyond it.
    CHECK(zone_limits::moved(pitchModel, start, 1, 45.0) == std::array<double, 3>{5.0, 45.0, 46.0});
    // In tune down to 3 pushes near perfect to 2.
    CHECK(zone_limits::moved(pitchModel, start, 1, 3.0) == std::array<double, 3>{2.0, 3.0, 40.0});
    // Out of tune down to 10 pushes in tune to 9, and that pushes nothing more.
    CHECK(zone_limits::moved(pitchModel, start, 2, 10.0) == std::array<double, 3>{5.0, 9.0, 10.0});
    // Out of tune all the way down pushes both, each a cent inside the next.
    CHECK(zone_limits::moved(pitchModel, start, 2, 0.0) == std::array<double, 3>{1.0, 2.0, 3.0});
    // A limit with room to spare moves its neighbours not at all.
    CHECK(zone_limits::moved(pitchModel, start, 1, 30.0) == std::array<double, 3>{5.0, 30.0, 40.0});
}

TEST_CASE("a push stops where a pushed limit would leave its range", "[widgets][zone-limits]")
{
    const std::array<double, 3> start{5.0, 25.0, 40.0};
    // In tune can go no higher than out of tune's 50, less a cent.
    CHECK(zone_limits::moved(pitchModel, start, 1, 50.0) == std::array<double, 3>{5.0, 49.0, 50.0});
    // Near perfect can go no higher than its own 20, whatever it would push.
    CHECK(zone_limits::moved(pitchModel, start, 0, 30.0)[0] == 20.0);
    // Pushed from above, near perfect stops at its 1 and in tune at 2.
    CHECK(zone_limits::moved(pitchModel, start, 1, -5.0) == std::array<double, 3>{1.0, 2.0, 40.0});
}

TEST_CASE("a move nobody can make leaves the limits as they are", "[widgets][zone-limits]")
{
    const std::array<double, 3> start{5.0, 25.0, 40.0};
    CHECK(zone_limits::moved(pitchModel, start, 1, std::nan("")) == start);
    CHECK(zone_limits::moved(pitchModel, start, 7, 20.0) == start);
}

TEST_CASE("a press takes hold of the nearest limit", "[widgets][zone-limits]")
{
    const std::array<double, 3> spread{5.0, 25.0, 40.0};
    CHECK(zone_limits::nearest(spread, 0.0) == 0);
    CHECK(zone_limits::nearest(spread, 20.0) == 1);
    CHECK(zone_limits::nearest(spread, 60.0) == 2);
    // Two that have met: the press beyond them takes the outer one, so they
    // can be pulled apart.
    const std::array<double, 3> met{5.0, 6.0, 6.0};
    CHECK(zone_limits::nearest(met, 9.0) == 2);
    CHECK(pitchModel.axisMaximum() == 50.0);
}
