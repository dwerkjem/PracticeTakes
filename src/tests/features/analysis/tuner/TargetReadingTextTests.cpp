#include <catch2/catch_test_macros.hpp>

#include "features/analysis/tuner/TargetReadingText.h"

#include <optional>

using pitch::readAgainstTarget;
using tuner::targetReadingText;

namespace
{
[[nodiscard]] pitch::Target b3()
{
    pitch::Target target;
    target.midiNoteNumber = 59;
    target.name = "B3";
    return target;
}

[[nodiscard]] juce::String said(std::optional<double> heard)
{
    return targetReadingText(b3(), readAgainstTarget(b3(), heard));
}

[[nodiscard]] juce::String utf8(const char* text)
{
    return juce::String::fromUTF8(text);
}
} // namespace

TEST_CASE("the tuner says where the note stands against the target", "[tuner][localisation]")
{
    REQUIRE(l10n::setLanguage("en"));

    CHECK(said(std::nullopt) == "Target B3");
    CHECK(said(59.03) == utf8("Target B3 \xC2\xB7 on it"));
    CHECK(said(58.88) == utf8("Target B3 \xC2\xB7 12 \xC2\xA2 flat"));
    CHECK(said(59.4) == utf8("Target B3 \xC2\xB7 40 \xC2\xA2 sharp"));
    CHECK(said(57.1) == utf8("Target B3 \xC2\xB7 2 semitones below"));
    CHECK(said(60.0) == utf8("Target B3 \xC2\xB7 1 semitone above"));
}
