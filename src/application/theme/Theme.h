#pragma once

#include "../../platform/pitch/IntonationZone.h"
#include "ThemeType.h"
#include <juce_graphics/juce_graphics.h>

// The four intonation zones' colours, the same in both themes: green near
// perfect, blue acceptable, amber near but off, red outside.
[[nodiscard]] juce::Colour intonationZoneColour(pitch::IntonationZone zone);

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

// The two rings around the tuner's marker (#372): a halo in the background's
// colour, then an outline in the foreground's. The marker is filled in its
// zone's colour, so on a lit band of the same colour only these set it apart;
// between them, one contrasts with every zone colour in either theme.
struct MarkerEdges
{
    juce::Colour halo;
    juce::Colour outline;
};

[[nodiscard]] MarkerEdges tunerMarkerEdges(Theme theme);

[[nodiscard]] AppPalette appPaletteFor(Theme theme);
[[nodiscard]] TunerPalette tunerPaletteFor(Theme theme);
[[nodiscard]] SpectrogramPalette spectrogramPaletteFor(Theme theme);
[[nodiscard]] juce::Colour spectrogramColourForLevel(Theme theme, float level);
