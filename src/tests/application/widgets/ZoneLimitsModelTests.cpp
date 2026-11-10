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

TEST_CASE("a dragged limit stops at its neighbour, a gap inside it", "[widgets][zone-limits]")
{
    const std::array<double, 3> start{5.0, 25.0, 40.0};
    // Near perfect cannot reach in tune; here its own maximum, 20, stops it first.
    const auto inner = zone_limits::moved(pitchModel, start, 0, 30.0);
    CHECK(inner == std::array<double, 3>{20.0, 25.0, 40.0});
    // In tune stops a cent short of out of tune, and of near perfect.
    CHECK(zone_limits::moved(pitchModel, start, 1, 45.0)[1] == 39.0);
    CHECK(zone_limits::moved(pitchModel, start, 1, 1.0)[1] == 6.0);
    // The others never move with it.
    const auto pulled = zone_limits::moved(pitchModel, start, 2, 10.0);
    CHECK(pulled[0] == 5.0);
    CHECK(pulled[1] == 25.0);
    CHECK(pulled[2] == 26.0);
}

TEST_CASE("a limit with no room left stays where it is", "[widgets][zone-limits]")
{
    // In tune pressed between two limits a gap apart has nowhere to go.
    const std::array<double, 3> squeezed{5.0, 6.0, 7.0};
    CHECK(zone_limits::moved(pitchModel, squeezed, 1, 20.0)[1] == 6.0);
    CHECK(zone_limits::moved(pitchModel, squeezed, 1, std::nan(""))[1] == 6.0);
    CHECK(zone_limits::moved(pitchModel, squeezed, 7, 20.0) == squeezed);
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
