#include <catch2/catch_test_macros.hpp>

#include "platform/pitch/IntonationZone.h"

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
