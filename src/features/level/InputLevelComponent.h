#pragma once

#include <JuceHeader.h>

#include "../../application/tools/ToolComponent.h"
#include "../../application/widgets/InputLevelMeter.h"
#include "InputLevelSettings.h"

#include <functional>

// The Input Level tool (#381): how loud the microphone is, the loudest it has
// been lately, and the line a take ignores sound below.
//
// The simple meter for 0.7.0. #25, the loudness meter with peak, RMS and LUFS
// views, is the full one and grows from this.
//
// The bar is InputLevelMeter, the same one Settings -> Practice draws beside
// the threshold, so the two never disagree about where the line is. The tool
// adds the figures, and says in words which side of the line the sound is on.
class InputLevelComponent final : public ToolComponent
{
  public:
    InputLevelComponent(
        AudioInputService& audioInputService,
        std::function<double()> thresholdDb,
        Theme initialTheme);

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void setTheme(Theme theme) override;
    void resetToDefaults() override;

    // Which way the bar runs: automatic, horizontal or vertical, chosen from
    // the tool's options menu and kept with the workspace.
    [[nodiscard]] std::optional<ToolSettingsPayload> captureSettings() const override;
    void applySettings(const ToolSettingsPayload& payload) override;
    [[nodiscard]] std::vector<MenuEntry> optionsMenuEntries() override;

    // "horizontal" or "vertical", for verification: the orientation forced.
    [[nodiscard]] bool showView(const juce::String& view) override;

  private:
    // "−32 dB", or below the scale.
    [[nodiscard]] static juce::String figure(double db);

    void setOrientation(input_level::Orientation choice);

    Theme theme;
    input_level::Orientation orientation = input_level::Orientation::automatic;
    InputLevelMeter meter;

    // Where the words go, set by resized() for paint().
    juce::Rectangle<int> levelArea;
    juce::Rectangle<int> statusArea;
    juce::Rectangle<int> detailArea;
    bool compactLayout = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InputLevelComponent)
};
