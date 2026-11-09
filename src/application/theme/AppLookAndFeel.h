#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "TextFonts.h"
#include "TextScale.h"

// The application look and feel, which exists to put one thing in place: the
// UI typeface.
//
// Practice Takes ships IBM Plex Sans embedded in the binary rather than asking
// the platform for it by name. Asking by name works on the machine of whoever
// installed the font and silently falls back to the platform sans everywhere
// else, and a silent fallback is the worst version of this: nothing fails, the
// window merely looks like a different application, and the only symptom is
// that screenshots stop matching.
//
// Regular and Bold are the two faces carried. JUCE resolves a font to plain or
// bold, so the design's 500 and 600 weights collapse onto these; see
// tools/packaging/fonts/README.md for why that is the honest mapping rather
// than a shortfall to fix later.
// A look-and-feel that knows the text size, so a component drawn with it can
// lay itself out for its text (global-text-size decision 3).
class TextScaleSource
{
  public:
    virtual ~TextScaleSource() = default;
    [[nodiscard]] virtual int textScale() const noexcept = 0;
};

class AppLookAndFeel final : public juce::LookAndFeel_V4, public TextScaleSource
{
  public:
    AppLookAndFeel();

    // Returns the embedded face for this font's weight. Called on every text
    // draw, so the typefaces are created once in the constructor rather than
    // parsed from the binary data on each call.
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& font) override;

    // JUCE centres button text. A button that acts as a disclosure row -- the
    // tuner's "Advanced settings  >" -- reads as one when its label starts at
    // the left edge and the chevron trails it, and reads as an ordinary button
    // when the whole thing floats in the middle.
    //
    // Opt-in per button rather than a blanket change, so the menu and window
    // buttons keep the centring they are drawn for. Set the property with
    // alignButtonTextLeft() below.
    void drawButtonText(
        juce::Graphics& graphics,
        juce::TextButton& button,
        bool shouldDrawButtonAsHighlighted,
        bool shouldDrawButtonAsDown) override;

    // Marks a button as one whose text hugs the left edge.
    static void alignButtonTextLeft(juce::Button& button);

    // The text size, as a percentage (appearance::TextScale): every widget
    // font below is JUCE's own size times this. At 100 they are JUCE's own
    // exactly, unrounded, so nothing moves until the setting does
    // (global-text-size decision 3).
    void setTextScale(int percent) noexcept;
    [[nodiscard]] int textScale() const noexcept override
    {
        return textScalePercent;
    }

    // JUCE's size `base` at the current text size.
    [[nodiscard]] juce::Font scaled(juce::Font base) const;

    // A label's own font, scaled: a component gives a label its role's *base*
    // size and this scales every label once. A combo box and a slider show
    // their text in a label, so they are scaled here too and not again.
    juce::Font getLabelFont(juce::Label& label) override;
    juce::Font getTextButtonFont(juce::TextButton& button, int buttonHeight) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getMenuBarFont(
        juce::MenuBarComponent& menuBar,
        int itemIndex,
        const juce::String& itemText) override;
    juce::Font getTabButtonFont(juce::TabBarButton& button, float height) override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;
    juce::Font getSliderPopupFont(juce::Slider& slider) override;

    // JUCE's toggle button draws its text at a size of its own, not through a
    // font function: this draws it the same way at the scaled size.
    void drawToggleButton(
        juce::Graphics& graphics,
        juce::ToggleButton& button,
        bool shouldDrawButtonAsHighlighted,
        bool shouldDrawButtonAsDown) override;

    // The family name the embedded faces report, for tests that need to
    // distinguish "the font loaded" from "something loaded".
    [[nodiscard]] static const char* embeddedFamilyName() noexcept;

    // Exposed so a test can assert both faces parsed. A null here means the
    // binary data did not make it into the target, which otherwise shows up
    // only as the wrong font in a screenshot nobody is diffing.
    [[nodiscard]] juce::Typeface::Ptr regularTypeface() const noexcept
    {
        return regular;
    }
    [[nodiscard]] juce::Typeface::Ptr boldTypeface() const noexcept
    {
        return bold;
    }

  private:
    juce::Typeface::Ptr regular;
    juce::Typeface::Ptr bold;
    int textScalePercent = appearance::TextScale::defaultPercent;
};

// The shell's windows and panels take the text size from the look-and-feel
// they are drawn with -- MainComponent's one, which it owns and sets -- and lay
// themselves out again in lookAndFeelChanged(), which a change of size sends
// (global-text-size decision 3). Tools are told through ToolComponent instead.
namespace appearance
{
// The text size `component` is drawn at: its look-and-feel's, or 100 under any
// other look-and-feel.
[[nodiscard]] inline int textScaleOf(const juce::Component& component)
{
    const auto* source = dynamic_cast<const TextScaleSource*>(&component.getLookAndFeel());
    return source != nullptr ? source->textScale() : TextScale::defaultPercent;
}

// A size laid out for 100 percent, grown with `component`'s text.
[[nodiscard]] inline int scaledFor(const juce::Component& component, int base)
{
    return scaledHeight(base, textScaleOf(component));
}

// `role`'s font for text `component` paints itself.
[[nodiscard]] inline juce::FontOptions
fontFor(const juce::Component& component, TextRole role, int styleFlags = juce::Font::plain)
{
    return textFont(role, textScaleOf(component), styleFlags);
}
} // namespace appearance

// The title strip's look-and-feel: JUCE's own, which the strip has always been
// drawn with, plus the text size for its buttons. Not AppLookAndFeel, whose
// colours and typeface would change how the strip looks at 100 percent.
class TitleStripLookAndFeel final : public juce::LookAndFeel_V4, public TextScaleSource
{
  public:
    void setTextScale(int percent) noexcept
    {
        textScalePercent = appearance::TextScale::snapped(static_cast<double>(percent));
    }

    [[nodiscard]] int textScale() const noexcept override
    {
        return textScalePercent;
    }

    juce::Font getTextButtonFont(juce::TextButton& button, int buttonHeight) override
    {
        return scaled(juce::LookAndFeel_V4::getTextButtonFont(button, buttonHeight));
    }

    // The transport's position readout, and the tempo's text box.
    juce::Font getLabelFont(juce::Label& label) override
    {
        return scaled(juce::LookAndFeel_V4::getLabelFont(label));
    }

    juce::Font getPopupMenuFont() override
    {
        return scaled(juce::LookAndFeel_V4::getPopupMenuFont());
    }

  private:
    [[nodiscard]] juce::Font scaled(juce::Font base) const
    {
        return textScalePercent == appearance::TextScale::defaultPercent
                   ? base
                   : base.withHeight(
                         appearance::TextScale::scaled(base.getHeight(), textScalePercent));
    }

    int textScalePercent = appearance::TextScale::defaultPercent;
};
