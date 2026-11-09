#include <catch2/catch_test_macros.hpp>

#include "platform/pitch/TargetReading.h"

using pitch::readAgainstTarget;
using pitch::TargetStanding;

namespace
{
[[nodiscard]] pitch::Target b3()
{
    pitch::Target target;
    target.midiNoteNumber = 59;
    target.name = "B3";
    return target;
}
} // namespace

TEST_CASE("the tuner reads the note it hears against the target", "[pitch][target]")
{
    CHECK(readAgainstTarget(b3(), std::nullopt).standing == TargetStanding::unheard);

    const auto on = readAgainstTarget(b3(), 59.03);
    CHECK(on.standing == TargetStanding::onIt);
    CHECK(on.amount == 0);

    const auto flat = readAgainstTarget(b3(), 58.88);
    CHECK(flat.standing == TargetStanding::flat);
    CHECK(flat.amount == 12);

    const auto sharp = readAgainstTarget(b3(), 59.4);
    CHECK(sharp.standing == TargetStanding::sharp);
    CHECK(sharp.amount == 40);

    const auto below = readAgainstTarget(b3(), 57.1);
    CHECK(below.standing == TargetStanding::below);
    CHECK(below.amount == 2);

    const auto above = readAgainstTarget(b3(), 60.0);
    CHECK(above.standing == TargetStanding::above);
    CHECK(above.amount == 1);
}

TEST_CASE("on it follows the singer's near-perfect limit", "[pitch][target]")
{
    CHECK(readAgainstTarget(b3(), 58.92).standing == TargetStanding::flat);
    CHECK(readAgainstTarget(b3(), 58.92, 10.0).standing == TargetStanding::onIt);
    CHECK(readAgainstTarget(b3(), 59.03, 2.0).standing == TargetStanding::sharp);
}
