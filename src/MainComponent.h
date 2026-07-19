#pragma once

#include <JuceHeader.h>

#include "AppDefaults.h"
#include "AudioInputService.h"
#include "Theme.h"

#include <memory>

// MainComponent is the application's central coordinator. It owns the shared
// audio device, the top-level controls, and the independent tool windows.
class MainComponent final : public juce::Component, private juce::ChangeListener
{
  public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

  private:
    enum class ToolType
    {
        tuner,
        spectrogram
    };

    class ToolWindow;
    class SettingsWindow;
    class MicrophoneWarning;

    // Initial setup ---------------------------------------------------------
    void configureTopButtons();
    void createMicrophoneWarning();

    // Tool and settings windows --------------------------------------------
    void showToolsMenu();
    void openTool(ToolType tool);
    void closeTool(ToolType tool);
    void showSettings();
    void closeSettings();
    void resetCurrentTool();
    void resetAudio();
    void resetLayout();
    void resetAll();
    void applyPreset(AppDefaults::Preset preset);
    void saveSettings();
    void loadSettings();

    [[nodiscard]] std::unique_ptr<juce::Component> createToolComponent(ToolType tool);
    [[nodiscard]] juce::String toolName(ToolType tool) const;
    [[nodiscard]] juce::Point<int> preferredToolWindowSize(ToolType tool) const;
    [[nodiscard]] std::unique_ptr<ToolWindow>& windowFor(ToolType tool);

    // Appearance ------------------------------------------------------------
    void setTheme(Theme theme);
    void applyAppearance();
    void configureLookAndFeelColours();
    void applyAppearanceToTopButtons();
    void applyAppearanceToOpenWindows();

    // Microphone state ------------------------------------------------------
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    [[nodiscard]] bool hasUsableMicrophone() const;
    void updateMicrophoneWarning();
    void dismissMicrophoneWarning();

    // One audio device manager is shared by every open analysis tool.
    AudioInputService audioInputService;
    juce::ApplicationProperties applicationProperties;
    juce::LookAndFeel_V4 appLookAndFeel;

    juce::TextButton fileButton{"File"};
    juce::TextButton settingsButton{"Settings"};
    juce::TextButton toolsButton{"Tools"};

    std::unique_ptr<ToolWindow> tunerWindow;
    std::unique_ptr<ToolWindow> spectrogramWindow;
    std::unique_ptr<SettingsWindow> settingsWindow;
    std::unique_ptr<MicrophoneWarning> microphoneWarning;

    juce::Rectangle<int> menuBarBounds;
    Theme currentTheme = Theme::light;
    ToolType currentTool = ToolType::tuner;
    AppDefaults::TunerSettings savedTunerSettings = AppDefaults::tunerDefaults();
    juce::Rectangle<int> savedTunerBounds;
    juce::Rectangle<int> savedSpectrogramBounds;
    juce::Rectangle<int> savedSettingsBounds;
    bool isMicrophoneWarningDismissed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
