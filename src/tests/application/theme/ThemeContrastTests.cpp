#include <catch2/catch_test_macros.hpp>

#include "application/theme/Theme.h"

#include <cmath>

namespace
{
[[nodiscard]] double linearChannel(juce::uint8 channel)
{
    const auto value = static_cast<double>(channel) / 255.0;
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

[[nodiscard]] double luminance(juce::Colour colour)
{
    return 0.2126 * linearChannel(colour.getRed()) + 0.7152 * linearChannel(colour.getGreen()) +
           0.0722 * linearChannel(colour.getBlue());
}

[[nodiscard]] double contrast(juce::Colour first, juce::Colour second)
{
    const auto lighter = std::max(luminance(first), luminance(second));
    const auto darker = std::min(luminance(first), luminance(second));
    return (lighter + 0.05) / (darker + 0.05);
}
} // namespace

TEST_CASE("light theme text meets normal-text contrast targets", "[theme][accessibility]")
{
    const auto palette = appPaletteFor(Theme::light);

    CHECK(contrast(palette.foreground, palette.background) >= 4.5);
    CHECK(contrast(palette.foreground, palette.panel) >= 4.5);
    CHECK(contrast(palette.foreground, palette.button) >= 4.5);
    CHECK(contrast(palette.muted, palette.panel) >= 4.5);
}

TEST_CASE("dark theme text meets normal-text contrast targets", "[theme][accessibility]")
{
    const auto palette = appPaletteFor(Theme::dark);

    CHECK(contrast(palette.foreground, palette.background) >= 4.5);
    CHECK(contrast(palette.foreground, palette.panel) >= 4.5);
    CHECK(contrast(palette.foreground, palette.button) >= 4.5);
    CHECK(contrast(palette.muted, palette.panel) >= 4.5);
}

TEST_CASE("custom tuner text remains readable in both themes", "[theme][accessibility][tuner]")
{
    for (const auto theme : {Theme::light, Theme::dark})
    {
        const auto palette = tunerPaletteFor(theme);

        CHECK(contrast(palette.foreground, palette.background) >= 4.5);
        CHECK(contrast(palette.foreground, palette.panel) >= 4.5);
        CHECK(contrast(palette.foreground, palette.control) >= 4.5);
        CHECK(contrast(palette.muted, palette.panel) >= 4.5);
    }
}

TEST_CASE(
    "custom spectrogram labels remain readable in both themes",
    "[theme][accessibility][spectrogram]")
{
    for (const auto theme : {Theme::light, Theme::dark})
    {
        const auto palette = spectrogramPaletteFor(theme);

        CHECK(contrast(palette.muted, palette.background) >= 4.5);
        CHECK(contrast(palette.muted, palette.panel) >= 4.5);
    }
}

TEST_CASE("spectrogram colour mapping clamps levels to its valid range", "[theme][spectrogram]")
{
    for (const auto theme : {Theme::light, Theme::dark})
    {
        CHECK(spectrogramColourForLevel(theme, -1.0f) == spectrogramColourForLevel(theme, 0.0f));
        CHECK(spectrogramColourForLevel(theme, 2.0f) == spectrogramColourForLevel(theme, 1.0f));
        CHECK(spectrogramColourForLevel(theme, 0.0f) != spectrogramColourForLevel(theme, 1.0f));
    }
}
