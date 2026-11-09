#include <catch2/catch_test_macros.hpp>

#include "platform/pitch/IntonationZone.h"

#include <cmath>

using pitch::IntonationZone;
using pitch::zoneOf;

TEST_CASE("a pitch is near perfect, acceptable, near but off, or outside", "[pitch][zone]")
{
    CHECK(zoneOf(0.0, 25.0) == IntonationZone::nearPerfect);
    CHECK(zoneOf(-5.0, 25.0) == IntonationZone::nearPerfect);
    CHECK(zoneOf(12.0, 25.0) == IntonationZone::acceptable);
    CHECK(zoneOf(-25.0, 25.0) == IntonationZone::acceptable);
    CHECK(zoneOf(30.0, 25.0) == IntonationZone::nearButOff);
    CHECK(zoneOf(-50.0, 25.0) == IntonationZone::nearButOff);
    CHECK(zoneOf(60.0, 25.0) == IntonationZone::outside);
    CHECK(zoneOf(-200.0, 25.0) == IntonationZone::outside);
}

TEST_CASE("the acceptable range follows the singer's tolerance", "[pitch][zone]")
{
    CHECK(zoneOf(15.0, 10.0) == IntonationZone::nearButOff);
    CHECK(zoneOf(15.0, 40.0) == IntonationZone::acceptable);

    // At the tightest tolerance there is no acceptable zone at all.
    CHECK(zoneOf(6.0, 5.0) == IntonationZone::nearButOff);
}

TEST_CASE("the near-perfect band follows the singer's setting", "[pitch][zone]")
{
    CHECK(zoneOf(8.0, 25.0) == IntonationZone::acceptable);
    CHECK(zoneOf(8.0, 25.0, 10.0) == IntonationZone::nearPerfect);
    CHECK(zoneOf(-3.0, pitch::ZoneLimits{2.0, 25.0}) == IntonationZone::acceptable);
}

TEST_CASE("each band's limit stays strictly inside the next looser one", "[pitch][zone]")
{
    using pitch::orderedLimits;
    using pitch::ZoneLimits;

    SECTION("limits already in order are kept")
    {
        CHECK(orderedLimits({5.0, 25.0}) == ZoneLimits{5.0, 25.0});
    }
    SECTION("in tune within stays below the near-note limit")
    {
        CHECK(orderedLimits({5.0, 50.0}).toleranceCents == pitch::nearNoteCents - 1.0);
        CHECK(orderedLimits({5.0, 80.0}).toleranceCents == pitch::maximumToleranceCents);
    }
    SECTION("near perfect gives way to in tune within, which decides verdicts")
    {
        CHECK(orderedLimits({20.0, 10.0}) == ZoneLimits{9.0, 10.0});
        CHECK(orderedLimits({10.0, 10.0}) == ZoneLimits{9.0, 10.0});
    }
    SECTION("each limit is held to its own range")
    {
        CHECK(orderedLimits({0.0, 25.0}).nearPerfectCents == pitch::minimumNearPerfectCents);
        CHECK(orderedLimits({30.0, 45.0}).nearPerfectCents == pitch::maximumNearPerfectCents);
        CHECK(orderedLimits({1.0, 1.0}) == ZoneLimits{1.0, pitch::minimumToleranceCents});
    }
    SECTION("a value that is not a number takes its default")
    {
        CHECK(orderedLimits({std::nan(""), std::nan("")}) == ZoneLimits{});
    }
    SECTION("whatever goes in, near perfect < in tune within < the near-note limit")
    {
        for (double nearPerfect = -5.0; nearPerfect <= 60.0; nearPerfect += 2.5)
        {
            for (double tolerance = -5.0; tolerance <= 60.0; tolerance += 2.5)
            {
                const auto limits = orderedLimits({nearPerfect, tolerance});
                CHECK(limits.nearPerfectCents < limits.toleranceCents);
                CHECK(limits.toleranceCents < pitch::nearNoteCents);
            }
        }
    }
}
