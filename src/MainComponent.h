#pragma once

#include <JuceHeader.h>

#include <memory>

class MainComponent final : public juce::Component
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

    void showToolsMenu();
    void openTool(ToolType tool);
    void closeTool(ToolType tool);
    void showSettings();
    void closeSettings();
    void setDarkMode(bool shouldUseDarkMode);
    void applyAppearance();

    juce::AudioDeviceManager audioDeviceManager;
    juce::LookAndFeel_V4 appLookAndFeel;

    juce::TextButton fileButton { "File" };
    juce::TextButton settingsButton { "Settings" };
    juce::TextButton toolsButton { "Tools" };

    std::unique_ptr<ToolWindow> tunerWindow;
    std::unique_ptr<ToolWindow> spectrogramWindow;
    std::unique_ptr<SettingsWindow> settingsWindow;

    juce::Rectangle<int> menuBarBounds;
    bool darkMode = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
