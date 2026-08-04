#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../theme/ThemeType.h"
#include "ToolSettingsPayload.h"

#include <optional>

// What the shell requires of a tool, and the whole of what it may assume.
//
// This exists so the shell can theme, reset, and persist any tool without
// knowing which one it is. It replaces the dynamic_cast ladders that used to
// name TunerComponent, SpectrogramComponent, and HarmonicAnalyzerComponent one
// after another -- the reason adding a fourth tool meant editing code that had
// nothing to do with it.
class ToolComponent : public juce::Component
{
  public:
    ~ToolComponent() override = default;

    // Called once at construction with the current theme, and again whenever
    // the user changes it.
    virtual void setTheme(Theme theme) = 0;

    virtual void resetToDefaults() = 0;

    // Per-instance settings. The default is a tool that persists nothing and
    // always starts at its defaults, which is what the spectrogram and the
    // harmonic analyzer do; a tool that persists overrides both.
    //
    // A tool that returns a payload must declare a settingsVersion in its
    // ToolDefinition. Restoring checks the two match and discards the payload
    // if they do not, so applySettings never sees a format it cannot read.
    [[nodiscard]] virtual std::optional<ToolSettingsPayload> captureSettings() const
    {
        return std::nullopt;
    }

    virtual void applySettings(const ToolSettingsPayload& payload)
    {
        juce::ignoreUnused(payload);
    }

    // Put the tool into one of its own named views, for verification.
    //
    // A tool that offers several ways of showing the same analysis -- the
    // tuner's graph, bar, and meter -- is three surfaces, not one, and a
    // screenshot of the default view says nothing about the other two. The
    // names are the tool's own, and the shell only passes them through: it
    // still has no idea which tool it is talking to, which is the whole point
    // of this class.
    //
    // Returns false for a view the tool does not have, so an approved state
    // naming a view that no longer exists fails loudly instead of quietly
    // capturing the default.
    [[nodiscard]] virtual bool showView(const juce::String& view)
    {
        juce::ignoreUnused(view);

        return false;
    }
};
