#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

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
class AppLookAndFeel final : public juce::LookAndFeel_V4
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
};
