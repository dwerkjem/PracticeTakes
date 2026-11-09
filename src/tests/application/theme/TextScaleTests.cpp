#include <catch2/catch_test_macros.hpp>

#include "application/theme/TextScale.h"

#include <limits>
#include <set>
#include <string_view>

using appearance::TextRole;
using appearance::TextScale;

TEST_CASE("the text size runs from 80 to 150 percent in tens", "[theme][text-size]")
{
    CHECK(TextScale::snapped(100.0) == 100);
    CHECK(TextScale::snapped(80.0) == 80);
    CHECK(TextScale::snapped(150.0) == 150);

    // Between steps, the nearest.
    CHECK(TextScale::snapped(114.0) == 110);
    CHECK(TextScale::snapped(116.0) == 120);

    // Out of range, the nearest end; not a number, the default.
    CHECK(TextScale::snapped(40.0) == 80);
    CHECK(TextScale::snapped(400.0) == 150);
    CHECK(TextScale::snapped(std::numeric_limits<double>::quiet_NaN()) == 100);
    CHECK(TextScale::snapped(std::numeric_limits<double>::infinity()) == 100);
}

TEST_CASE("a scaled size is rounded to half a point", "[theme][text-size]")
{
    CHECK(TextScale::scaled(12.0f, 150) == 18.0f);
    CHECK(TextScale::scaled(13.0f, 110) == 14.5f); // 14.3
    CHECK(TextScale::scaled(11.0f, 120) == 13.0f); // 13.2
    CHECK(TextScale::scaled(9.0f, 80) == 7.0f);    // 7.2
    CHECK(TextScale::factor(150) == 1.5);
}

TEST_CASE("at 100 percent every role is exactly its base size", "[theme][text-size]")
{
    // So the change moves nothing until the setting does.
    for (const auto& info : appearance::textRoles)
    {
        INFO(info.name);
        CHECK(appearance::textSize(info.role, 100) == info.baseSize);
    }
}

TEST_CASE("the roles are today's sizes, each named once", "[theme][text-size]")
{
    std::set<std::string_view> names;
    std::set<float> sizes;
    for (const auto& info : appearance::textRoles)
    {
        CHECK_FALSE(info.name.empty());
        CHECK(names.insert(info.name).second);
        sizes.insert(info.baseSize);
        CHECK(appearance::roleInfo(info.role).name == info.name);
    }

    // Every size drawn before this change, and JUCE's own for menus (17),
    // buttons and choices (16) and labels (15).
    CHECK(
        sizes == std::set<float>{
                     9.0f, 10.0f, 11.0f, 11.5f, 12.0f, 13.0f, 14.0f, 14.5f, 15.0f, 16.0f, 17.0f,
                     18.0f, 20.0f, 22.0f, 30.0f, 34.0f});
    CHECK(appearance::baseSize(TextRole::small) == 12.0f);
    CHECK(appearance::textSize(TextRole::small, 150) == 18.0f);
}
