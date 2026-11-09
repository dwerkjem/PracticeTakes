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
    CHECK(readAgainstTarget(b3(), std::nullopt).text == "Target B3");
    CHECK(readAgainstTarget(b3(), std::nullopt).standing == TargetStanding::unheard);

    const auto on = readAgainstTarget(b3(), 59.03);
    CHECK(on.standing == TargetStanding::onIt);
    CHECK(on.text == "Target B3 \xC2\xB7 on it");

    const auto flat = readAgainstTarget(b3(), 58.88);
    CHECK(flat.standing == TargetStanding::flat);
    CHECK(flat.amount == 12);
    CHECK(flat.text == "Target B3 \xC2\xB7 12 \xC2\xA2 flat");

    const auto sharp = readAgainstTarget(b3(), 59.4);
    CHECK(sharp.text == "Target B3 \xC2\xB7 40 \xC2\xA2 sharp");

    const auto below = readAgainstTarget(b3(), 57.1);
    CHECK(below.standing == TargetStanding::below);
    CHECK(below.text == "Target B3 \xC2\xB7 2 semitones below");

    CHECK(readAgainstTarget(b3(), 60.0).text == "Target B3 \xC2\xB7 1 semitone above");
}

TEST_CASE("on it follows the singer's near-perfect limit", "[pitch][target]")
{
    CHECK(readAgainstTarget(b3(), 58.92).standing == TargetStanding::flat);
    CHECK(readAgainstTarget(b3(), 58.92, 10.0).standing == TargetStanding::onIt);
    CHECK(readAgainstTarget(b3(), 59.03, 2.0).standing == TargetStanding::sharp);
}
