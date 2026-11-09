#include <catch2/catch_test_macros.hpp>

#include "features/analysis/tuner/TunerZones.h"

#include <cmath>
#include <optional>
#include <vector>

using pitch::IntonationZone;
using tuner::BandLook;
using tuner::zoneBands;

namespace
{
[[nodiscard]] std::vector<BandLook> looksOf(const std::vector<tuner::ZoneBand>& bands)
{
    std::vector<BandLook> looks;
    for (const auto& band : bands)
    {
        looks.push_back(band.look);
    }
    return looks;
}

[[nodiscard]] pitch::ZoneLimits limits(double nearPerfect, double tolerance)
{
    return {nearPerfect, tolerance};
}
} // namespace

TEST_CASE("each tuner zone is as wide as its own range", "[tuner][zones]")
{
    const auto bands = zoneBands(std::nullopt, limits(5.0, 15.0));

    REQUIRE(bands.size() == 4);
    CHECK(bands[0].zone == IntonationZone::nearPerfect);
    CHECK(bands[0].innerCents == 0.0);
    CHECK(bands[0].outerCents == 5.0);
    CHECK(bands[1].zone == IntonationZone::acceptable);
    CHECK(bands[1].innerCents == 5.0);
    CHECK(bands[1].outerCents == 15.0);
    CHECK(bands[2].zone == IntonationZone::nearButOff);
    CHECK(bands[2].innerCents == 15.0);
    CHECK(bands[2].outerCents == pitch::outOfTuneCents);

    // Red is everything past the out-of-tune limit, either way.
    CHECK(bands[3].zone == IntonationZone::outside);
    CHECK(bands[3].innerCents == pitch::outOfTuneCents);
    CHECK(std::isinf(bands[3].outerCents));
}

TEST_CASE("the tuner's red band begins at the singer's out-of-tune limit", "[tuner][zones]")
{
    const auto bands = zoneBands(42.0, pitch::ZoneLimits{5.0, 25.0, 45.0});

    CHECK(bands[2].outerCents == 45.0);
    CHECK(bands[3].innerCents == 45.0);
    CHECK(bands[2].look == BandLook::lit);
    CHECK(bands[3].look == BandLook::dotted);
}

TEST_CASE("the tuner's near-perfect band follows the singer's setting", "[tuner][zones]")
{
    const auto bands = zoneBands(8.0, limits(10.0, 30.0));

    CHECK(bands[0].outerCents == 10.0);
    CHECK(bands[1].innerCents == 10.0);
    CHECK(bands[0].look == BandLook::lit);
}

TEST_CASE("with no signal every tuner zone is tinted and none is lit", "[tuner][zones]")
{
    CHECK(
        looksOf(zoneBands(std::nullopt, limits(5.0, 25.0))) ==
        std::vector{BandLook::tinted, BandLook::tinted, BandLook::tinted, BandLook::tinted});
}

TEST_CASE(
    "the zone the pitch is in is lit, tighter ones tinted, looser ones dotted",
    "[tuner][zones]")
{
    const auto usual = limits(5.0, 25.0);

    SECTION("near perfect: only its band is lit, every looser band is a dotted line")
    {
        CHECK(
            looksOf(zoneBands(-3.0, usual)) ==
            std::vector{BandLook::lit, BandLook::dotted, BandLook::dotted, BandLook::dotted});
    }
    SECTION("acceptable: near perfect stays tinted inside it, the looser two are dotted")
    {
        CHECK(
            looksOf(zoneBands(12.0, usual)) ==
            std::vector{BandLook::tinted, BandLook::lit, BandLook::dotted, BandLook::dotted});
    }
    SECTION("near but off: both tighter bands are tinted, red is dotted")
    {
        CHECK(
            looksOf(zoneBands(-30.0, usual)) ==
            std::vector{BandLook::tinted, BandLook::tinted, BandLook::lit, BandLook::dotted});
    }
    SECTION("outside: red is lit, every tighter band tinted")
    {
        const std::vector expected{
            BandLook::tinted, BandLook::tinted, BandLook::tinted, BandLook::lit};
        CHECK(looksOf(zoneBands(55.0, usual)) == expected);
        CHECK(looksOf(zoneBands(-300.0, usual)) == expected);
    }
}

TEST_CASE("a band's edge belongs to the tighter zone, as in pitch::zoneOf", "[tuner][zones]")
{
    CHECK(zoneBands(5.0, limits(5.0, 25.0))[0].look == BandLook::lit);
    CHECK(zoneBands(25.0, limits(5.0, 25.0))[1].look == BandLook::lit);
    CHECK(zoneBands(40.0, limits(5.0, 25.0))[2].look == BandLook::lit);
    CHECK(zoneBands(40.5, limits(5.0, 25.0))[3].look == BandLook::lit);
}

TEST_CASE("crossed limits still give four bands, each inside the next", "[tuner][zones]")
{
    const auto bands = zoneBands(std::nullopt, limits(20.0, 10.0));

    REQUIRE(bands.size() == 4);
    CHECK(bands[0].outerCents < bands[1].outerCents);
    CHECK(bands[1].outerCents < bands[2].outerCents);
    CHECK(bands[2].outerCents < bands[3].outerCents);
    CHECK(bands[1].outerCents == 10.0);
}

TEST_CASE("only the half of the lit zone the pitch is on lights", "[tuner][zones]")
{
    using tuner::Side;
    const auto usual = limits(5.0, 25.0);

    CHECK(zoneBands(12.0, usual)[1].litSide == Side::sharp);
    CHECK(zoneBands(-12.0, usual)[1].litSide == Side::flat);
    CHECK(zoneBands(-2.0, usual)[0].litSide == Side::flat);
    CHECK(zoneBands(45.0, usual)[3].litSide == Side::sharp);

    // Dead on the note, both halves; and a band that is not lit has no side.
    CHECK(zoneBands(0.0, usual)[0].litSide == Side::both);
    CHECK(zoneBands(12.0, usual)[0].litSide == Side::both);
    CHECK(zoneBands(std::nullopt, usual)[1].litSide == Side::both);
}
