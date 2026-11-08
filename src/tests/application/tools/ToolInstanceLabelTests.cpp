#include <catch2/catch_test_macros.hpp>

#include "application/tools/ToolInstanceLabel.h"

#include <string>

TEST_CASE("the first instance keeps the tool's plain name", "[tools][label]")
{
    CHECK(ToolInstanceLabel::defaultName("Tuner", 1) == "Tuner");
}

TEST_CASE("later instances carry their ordinal", "[tools][label]")
{
    CHECK(ToolInstanceLabel::defaultName("Tuner", 2) == "Tuner 2");
    CHECK(ToolInstanceLabel::defaultName("Harmonic Analyzer", 7) == "Harmonic Analyzer 7");
}

TEST_CASE("a label wins over the default name", "[tools][label]")
{
    CHECK(ToolInstanceLabel::displayName("Tuner", 2, "Violin") == "Violin");
}

TEST_CASE("an empty label falls back to the default name", "[tools][label]")
{
    CHECK(ToolInstanceLabel::displayName("Tuner", 2, "") == "Tuner 2");
}

TEST_CASE("sanitising trims and removes control characters", "[tools][label]")
{
    CHECK(ToolInstanceLabel::sanitise("  Violin  ") == "Violin");
    CHECK(ToolInstanceLabel::sanitise("Vio\nlin\t") == "Violin");
    CHECK(
        ToolInstanceLabel::sanitise(std::string("Vi\x7f"
                                                "olin")) == "Violin");
}

TEST_CASE("a label of only spaces or control characters means the default", "[tools][label]")
{
    CHECK(ToolInstanceLabel::sanitise("   ").empty());
    CHECK(ToolInstanceLabel::sanitise("\n\r\t").empty());
}

TEST_CASE("a label is cut to the maximum length", "[tools][label]")
{
    const std::string longLabel(ToolInstanceLabel::maximumCharacters + 10, 'a');
    CHECK(
        ToolInstanceLabel::sanitise(longLabel) ==
        std::string(ToolInstanceLabel::maximumCharacters, 'a'));
}

TEST_CASE("cutting a label never splits a multi-byte character", "[tools][label]")
{
    // "é" is two bytes in UTF-8. A label of maximum + 1 of them must come back
    // as exactly the maximum number of whole characters.
    std::string accented;
    for (std::size_t index = 0; index <= ToolInstanceLabel::maximumCharacters; ++index)
    {
        accented += "\xc3\xa9";
    }

    const auto cut = ToolInstanceLabel::sanitise(accented);
    CHECK(cut.size() == ToolInstanceLabel::maximumCharacters * 2);
    CHECK(cut.substr(0, 2) == "\xc3\xa9");
}

TEST_CASE("cutting does not leave a trailing space", "[tools][label]")
{
    std::string label(ToolInstanceLabel::maximumCharacters - 1, 'a');
    label += " tail";
    const auto cut = ToolInstanceLabel::sanitise(label);
    CHECK(cut == std::string(ToolInstanceLabel::maximumCharacters - 1, 'a'));
}
