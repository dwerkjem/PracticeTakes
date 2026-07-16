#pragma once

#include <JuceHeader.h>

#include <array>
#include <memory>

class TunerComponent final : public juce::Component,
                             private juce::AudioSource,
                             private juce::ChangeListener,
                             private juce::Timer
{
public:
    TunerComponent();
    ~TunerComponent() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    static constexpr int fifoCapacity = 32768;
    static constexpr int analysisSize = 4096;

    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void showAudioDeviceSelector();
    void hideAudioDeviceSelector();
    void updateAudioDeviceStatus();
    [[nodiscard]] bool hasUsableInputDevice() const;
    void attachAudioCallbackIfPossible();
    void detachAudioCallback();
    void drainAudioFifo();
    [[nodiscard]] double detectPitch() const;
    void updateNote(double frequency);

    juce::AudioDeviceManager audioDeviceManager;
    juce::AudioSourcePlayer audioSourcePlayer;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> audioDeviceSelector;
    juce::Label microphoneLabel;
    juce::TextButton microphoneButton { "Select microphone..." };
    juce::TextButton audioSettingsDoneButton { "Done" };
    juce::String audioErrorMessage;

    juce::AbstractFifo audioFifo { fifoCapacity };
    std::array<float, fifoCapacity> fifoBuffer {};
    std::array<float, analysisSize> analysisBuffer {};

    double currentSampleRate = 44100.0;
    double displayedFrequency = 0.0;
    double displayedCents = 0.0;
    juce::String displayedNote { "--" };
    float inputLevel = 0.0f;
    bool hasSignal = false;
    bool audioCallbackAttached = false;
    bool showingAudioDeviceSelector = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TunerComponent)
};
