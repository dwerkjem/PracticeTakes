#pragma once

#include <JuceHeader.h>

#include "SpectrogramComponent.h"
#include "TunerComponent.h"

#include <memory>

class MainComponent final : public juce::Component,
                            private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void showAudioSettings();
    void hideAudioSettings();
    void updateMicrophoneLabel();
    void toggleTuner();
    void toggleSpectrogram();
    void updateToolVisibility();

    juce::AudioDeviceManager audioDeviceManager;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> audioDeviceSelector;

    juce::Label appTitle;
    juce::Label appSubtitle;
    juce::Label microphoneLabel;
    juce::Label workspaceLabel;
    juce::TextButton settingsButton { "Audio settings" };
    juce::TextButton settingsDoneButton { "Done" };
    juce::ToggleButton tunerToggle { "Tuner" };
    juce::ToggleButton spectrogramToggle { "Spectrogram" };

    std::unique_ptr<TunerComponent> tunerComponent;
    std::unique_ptr<SpectrogramComponent> spectrogramComponent;
    juce::Rectangle<int> headerBounds;
    juce::Rectangle<int> toolbarBounds;
    juce::Rectangle<int> workspaceBounds;
    juce::String audioErrorMessage;
    bool showingAudioSettings = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
