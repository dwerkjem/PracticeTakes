#pragma once

#include "ThemeType.h"
#include <JuceHeader.h>

struct AppPalette
{
    juce::Colour background;
    juce::Colour panel;
    juce::Colour button;
    juce::Colour buttonHover;
    juce::Colour foreground;
    juce::Colour muted;
    juce::Colour outline;
    juce::Colour accent;
    juce::Colour warning;
    juce::Colour menuBarTop;
    juce::Colour menuBarBottom;
};

struct TunerPalette
{
    juce::Colour background;
    juce::Colour panel;
    juce::Colour control;
    juce::Colour outline;
    juce::Colour foreground;
    juce::Colour muted;
    juce::Colour accent;
    juce::Colour inTune;
};

struct SpectrogramPalette
{
    juce::Colour background;
    juce::Colour panel;
    juce::Colour muted;
    juce::Colour outline;
};

[[nodiscard]] AppPalette appPaletteFor(Theme theme);
[[nodiscard]] TunerPalette tunerPaletteFor(Theme theme);
[[nodiscard]] SpectrogramPalette spectrogramPaletteFor(Theme theme);
[[nodiscard]] juce::Colour spectrogramColourForLevel(Theme theme, float level);
