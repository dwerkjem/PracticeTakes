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

TEST_CASE(
    "the tuner's marker stands out from every zone colour in both themes",
    "[theme][accessibility][tuner]")
{
    // #372: the marker is filled in its zone's colour and sits on a lit band
    // of the same colour, so its rings are all that set it apart. One of the
    // two must reach the 3:1 a user-interface graphic needs against every
    // zone, and the two must be told apart from each other.
    for (const Theme theme : {Theme::light, Theme::dark})
    {
        const MarkerEdges edges = tunerMarkerEdges(theme);
        CHECK(contrast(edges.halo, edges.outline) >= 3.0);
        for (const auto zone :
             {pitch::IntonationZone::nearPerfect, pitch::IntonationZone::acceptable,
              pitch::IntonationZone::nearButOff, pitch::IntonationZone::outside})
        {
            const auto colour = intonationZoneColour(zone);
            INFO("dark " << isDarkTheme(theme) << ", zone " << static_cast<int>(zone));
            CHECK(std::max(contrast(edges.halo, colour), contrast(edges.outline, colour)) >= 3.0);
        }
    }
}
