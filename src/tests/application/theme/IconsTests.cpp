#include <catch2/catch_test_macros.hpp>

#include "application/theme/Icons.h"

using appearance::Icon;

TEST_CASE("every icon is embedded and parses", "[theme][icons]")
{
    for (int index = static_cast<int>(Icon::globe); index <= static_cast<int>(Icon::presentation);
         ++index)
    {
        const auto icon = static_cast<Icon>(index);
        CAPTURE(index);
        CHECK_FALSE(appearance::iconResourceName(icon).empty());
        const juce::Drawable* drawable = appearance::iconDrawable(icon, juce::Colours::black);
        REQUIRE(drawable != nullptr);
        CHECK_FALSE(drawable->getDrawableBounds().isEmpty());
    }
    CHECK(appearance::iconDrawable(Icon::none, juce::Colours::black) == nullptr);
}

TEST_CASE("an icon is drawn in the colour asked for, and kept", "[theme][icons]")
{
    const juce::Drawable* white = appearance::iconDrawable(Icon::globe, juce::Colours::white);
    const juce::Drawable* again = appearance::iconDrawable(Icon::globe, juce::Colours::white);
    const juce::Drawable* red = appearance::iconDrawable(Icon::globe, juce::Colours::red);
    CHECK(white == again);
    CHECK(white != red);

    juce::Image image(juce::Image::ARGB, 24, 24, true);
    {
        juce::Graphics graphics(image);
        appearance::drawIcon(graphics, Icon::globe, {0.0f, 0.0f, 24.0f, 24.0f}, juce::Colours::red);
    }
    bool anyRed = false;
    for (int y = 0; y < 24 && !anyRed; ++y)
    {
        for (int x = 0; x < 24 && !anyRed; ++x)
        {
            const auto pixel = image.getPixelAt(x, y);
            anyRed = pixel.getAlpha() > 128 && pixel.getRed() > 200 && pixel.getGreen() < 60;
        }
    }
    CHECK(anyRed);
}

TEST_CASE("a component carries its icon", "[theme][icons]")
{
    juce::Label label;
    CHECK(appearance::iconOf(label) == Icon::none);
    appearance::setIcon(label, Icon::globe);
    CHECK(appearance::iconOf(label) == Icon::globe);

    const auto item = appearance::menuItem(7, "Language", Icon::globe, juce::Colours::black);
    CHECK(item.itemID == 7);
    CHECK(item.image != nullptr);
}
