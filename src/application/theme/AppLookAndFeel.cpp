#include "AppLookAndFeel.h"

#include <PracticeTakesFonts.h>

namespace
{
// What the embedded faces report as their family. Checked by
// AppLookAndFeelTests so that "a typeface loaded" cannot be mistaken for "the
// right typeface loaded".
constexpr const char* plexSansFamily = "IBM Plex Sans";

// Component property naming the opt-in above. A property rather than a subclass
// so a plain juce::TextButton can ask for it without every caller needing a new
// type.
const juce::Identifier leftAlignedTextProperty{"practiceTakesLeftAlignedText"};

// The design's .adv rule is padding: 0 12px.
constexpr int leftAlignedTextIndent = 12;
} // namespace

AppLookAndFeel::AppLookAndFeel()
    : regular(juce::Typeface::createSystemTypefaceFor(
          PracticeTakesFonts::IBMPlexSansRegular_ttf,
          static_cast<std::size_t>(PracticeTakesFonts::IBMPlexSansRegular_ttfSize))),
      bold(juce::Typeface::createSystemTypefaceFor(
          PracticeTakesFonts::IBMPlexSansBold_ttf,
          static_cast<std::size_t>(PracticeTakesFonts::IBMPlexSansBold_ttfSize)))
{
}

juce::Typeface::Ptr AppLookAndFeel::getTypefaceForFont(const juce::Font& font)
{
    const auto& wanted = font.isBold() ? bold : regular;

    // Fall back to the base class rather than returning null if the embedded
    // data is somehow missing: a window drawn in the platform sans is a worse
    // outcome than one drawn in Plex, but both are better than no text at all.
    if (wanted == nullptr)
    {
        return juce::LookAndFeel_V4::getTypefaceForFont(font);
    }

    return wanted;
}

const char* AppLookAndFeel::embeddedFamilyName() noexcept
{
    return plexSansFamily;
}

void AppLookAndFeel::alignButtonTextLeft(juce::Button& button)
{
    button.getProperties().set(leftAlignedTextProperty, true);
}

void AppLookAndFeel::drawButtonText(
    juce::Graphics& graphics,
    juce::TextButton& button,
    bool shouldDrawButtonAsHighlighted,
    bool shouldDrawButtonAsDown)
{
    if (!button.getProperties()[leftAlignedTextProperty])
    {
        juce::LookAndFeel_V4::drawButtonText(
            graphics, button, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
        return;
    }

    // Same colour and vertical metrics as the base class; only the horizontal
    // inset and the justification differ, so an opted-in button still matches
    // every other button in weight, colour, and baseline.
    const auto font = getTextButtonFont(button, button.getHeight());
    graphics.setFont(font);
    graphics.setColour(button
                           .findColour(
                               button.getToggleState() ? juce::TextButton::textColourOnId
                                                       : juce::TextButton::textColourOffId)
                           .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));

    const auto yIndent = juce::jmin(4, button.proportionOfHeight(0.3f));
    const auto textWidth = button.getWidth() - leftAlignedTextIndent * 2;

    if (textWidth > 0)
    {
        graphics.drawFittedText(
            button.getButtonText(), leftAlignedTextIndent, yIndent, textWidth,
            button.getHeight() - yIndent * 2, juce::Justification::centredLeft, 1);
    }
}

void AppLookAndFeel::setTextScale(int percent) noexcept
{
    textScalePercent = appearance::TextScale::snapped(static_cast<double>(percent));
}

juce::Font AppLookAndFeel::scaled(const juce::Font& base) const
{
    if (textScalePercent == appearance::TextScale::defaultPercent)
    {
        return base;
    }
    return base.withHeight(appearance::TextScale::scaled(base.getHeight(), textScalePercent));
}

juce::Font AppLookAndFeel::getLabelFont(juce::Label& label)
{
    return scaled(juce::LookAndFeel_V4::getLabelFont(label));
}

juce::Font AppLookAndFeel::getTextButtonFont(juce::TextButton& button, int buttonHeight)
{
    return scaled(juce::LookAndFeel_V4::getTextButtonFont(button, buttonHeight));
}

juce::Font AppLookAndFeel::getPopupMenuFont()
{
    return scaled(juce::LookAndFeel_V4::getPopupMenuFont());
}

juce::Font AppLookAndFeel::getMenuBarFont(
    juce::MenuBarComponent& menuBar,
    int itemIndex,
    const juce::String& itemText)
{
    return scaled(juce::LookAndFeel_V4::getMenuBarFont(menuBar, itemIndex, itemText));
}

juce::Font AppLookAndFeel::getTabButtonFont(juce::TabBarButton& button, float height)
{
    return scaled(juce::LookAndFeel_V4::getTabButtonFont(button, height));
}

juce::Font AppLookAndFeel::getAlertWindowTitleFont()
{
    return scaled(juce::LookAndFeel_V4::getAlertWindowTitleFont());
}

juce::Font AppLookAndFeel::getAlertWindowMessageFont()
{
    return scaled(juce::LookAndFeel_V4::getAlertWindowMessageFont());
}

juce::Font AppLookAndFeel::getAlertWindowFont()
{
    return scaled(juce::LookAndFeel_V4::getAlertWindowFont());
}

juce::Font AppLookAndFeel::getSliderPopupFont(juce::Slider& slider)
{
    return scaled(juce::LookAndFeel_V4::getSliderPopupFont(slider));
}

void AppLookAndFeel::drawToggleButton(
    juce::Graphics& graphics,
    juce::ToggleButton& button,
    bool shouldDrawButtonAsHighlighted,
    bool shouldDrawButtonAsDown)
{
    if (textScalePercent == appearance::TextScale::defaultPercent)
    {
        juce::LookAndFeel_V4::drawToggleButton(
            graphics, button, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
        return;
    }

    // LookAndFeel_V4::drawToggleButton, with its text size scaled: the box
    // grows with the text, so the two stay in proportion.
    const float fontSize = appearance::TextScale::scaled(
        juce::jmin(15.0f, static_cast<float>(button.getHeight()) * 0.75f), textScalePercent);
    const float tickWidth = fontSize * 1.1f;

    drawTickBox(
        graphics, button, 4.0f, (static_cast<float>(button.getHeight()) - tickWidth) * 0.5f,
        tickWidth, tickWidth, button.getToggleState(), button.isEnabled(),
        shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

    graphics.setColour(button.findColour(juce::ToggleButton::textColourId));
    graphics.setFont(juce::FontOptions(fontSize));
    if (!button.isEnabled())
    {
        graphics.setOpacity(0.5f);
    }
    graphics.drawFittedText(
        button.getButtonText(),
        button.getLocalBounds()
            .withTrimmedLeft(juce::roundToInt(tickWidth) + 10)
            .withTrimmedRight(2),
        juce::Justification::centredLeft, 10);
}
