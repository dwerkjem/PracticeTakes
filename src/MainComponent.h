#pragma once

#include <JuceHeader.h>

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
    void initialiseAudioDevice();
    void createMicrophoneWarning();

    // Tool and settings windows --------------------------------------------
    void showToolsMenu();
    void openTool(ToolType tool);
    void closeTool(ToolType tool);
    void showSettings();
    void closeSettings();

    [[nodiscard]] std::unique_ptr<juce::Component> createToolComponent(ToolType tool);
    [[nodiscard]] juce::String toolName(ToolType tool) const;
    [[nodiscard]] juce::Point<int> preferredToolWindowSize(ToolType tool) const;
    [[nodiscard]] std::unique_ptr<ToolWindow>& windowFor(ToolType tool);

    // Appearance ------------------------------------------------------------
    void setDarkMode(bool shouldUseDarkMode);
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
    juce::AudioDeviceManager audioDeviceManager;
    juce::LookAndFeel_V4 appLookAndFeel;

    juce::TextButton fileButton{"File"};
    juce::TextButton settingsButton{"Settings"};
    juce::TextButton toolsButton{"Tools"};

    std::unique_ptr<ToolWindow> tunerWindow;
    std::unique_ptr<ToolWindow> spectrogramWindow;
    std::unique_ptr<SettingsWindow> settingsWindow;
    std::unique_ptr<MicrophoneWarning> microphoneWarning;

    juce::Rectangle<int> menuBarBounds;
    bool isDarkMode = false;
    bool isMicrophoneWarningDismissed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
