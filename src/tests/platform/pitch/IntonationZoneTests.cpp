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
    CHECK(zoneOf(-40.0, 25.0) == IntonationZone::nearButOff);
    CHECK(zoneOf(41.0, 25.0) == IntonationZone::outside);
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

TEST_CASE("out of tune begins at the singer's limit, 40 cents by default", "[pitch][zone]")
{
    CHECK(pitch::outOfTuneCents == 40.0);
    CHECK(zoneOf(45.0, 25.0) == IntonationZone::outside);
    CHECK(zoneOf(45.0, 25.0, 5.0, 50.0) == IntonationZone::nearButOff);
    CHECK(zoneOf(-28.0, pitch::ZoneLimits{5.0, 25.0, 27.0}) == IntonationZone::outside);
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
    SECTION("out of tune gives way to in tune within, and stops at the near-note limit")
    {
        CHECK(orderedLimits({5.0, 40.0, 35.0}) == ZoneLimits{5.0, 40.0, 41.0});
        CHECK(orderedLimits({5.0, 25.0, 70.0}).outOfTuneCents == pitch::nearNoteCents);
        CHECK(orderedLimits({5.0, 49.0, 35.0}).outOfTuneCents == pitch::nearNoteCents);
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
        CHECK(orderedLimits({std::nan(""), std::nan(""), std::nan("")}) == ZoneLimits{});
    }
    SECTION("whatever goes in, near perfect < in tune within < out of tune <= 50")
    {
        for (double nearPerfect = -5.0; nearPerfect <= 60.0; nearPerfect += 5.0)
        {
            for (double tolerance = -5.0; tolerance <= 60.0; tolerance += 5.0)
            {
                for (double outOfTune = -5.0; outOfTune <= 60.0; outOfTune += 5.0)
                {
                    const auto limits = orderedLimits({nearPerfect, tolerance, outOfTune});
                    CHECK(limits.nearPerfectCents < limits.toleranceCents);
                    CHECK(limits.toleranceCents < limits.outOfTuneCents);
                    CHECK(limits.outOfTuneCents <= pitch::nearNoteCents);
                }
            }
        }
    }
}
