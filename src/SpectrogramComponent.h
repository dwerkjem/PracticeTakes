#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>

class SpectrogramComponent final : public juce::Component,
                                    private juce::AudioIODeviceCallback,
                                    private juce::ChangeListener,
                                    private juce::Timer
{
public:
    explicit SpectrogramComponent(
        juce::AudioDeviceManager& sharedAudioDeviceManager);
    ~SpectrogramComponent() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void setDarkMode(bool shouldUseDarkMode);

private:
    static constexpr int fifoCapacity = 65536;
    static constexpr int fftOrder = 10;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int imageWidth = 720;
    static constexpr int imageHeight = 360;

    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    void changeListenerCallback(
        juce::ChangeBroadcaster* source) override;
    void timerCallback() override;

    [[nodiscard]] bool hasUsableInputDevice() const;
    void updateAudioDeviceStatus();
    void attachAudioCallbackIfPossible();
    void detachAudioCallback();
    void calculateNextColumn();
    void updateSpectrogramColumn();
    [[nodiscard]] juce::Colour colourForLevel(float level) const;
    [[nodiscard]] float yForFrequency(double frequency) const;
    [[nodiscard]] juce::Colour backgroundColour() const;
    [[nodiscard]] juce::Colour panelColour() const;
    [[nodiscard]] juce::Colour foregroundColour() const;
    [[nodiscard]] juce::Colour mutedColour() const;
    [[nodiscard]] juce::Colour outlineColour() const;

    juce::AudioDeviceManager& audioDeviceManager;
    juce::Rectangle<int> spectrogramBounds;

    juce::AbstractFifo audioFifo { fifoCapacity };
    std::array<float, fifoCapacity> fifoBuffer {};
    std::array<float, fftSize * 2> fftData {};

    juce::dsp::FFT forwardFFT { fftOrder };
    juce::dsp::WindowingFunction<float> window {
        fftSize,
        juce::dsp::WindowingFunction<float>::hann
    };
    juce::Image spectrogramImage {
        juce::Image::RGB,
        imageWidth,
        imageHeight,
        true
    };

    std::atomic<double> currentSampleRate { 44100.0 };
    bool audioCallbackAttached = false;
    bool darkMode = false;
    juce::String audioErrorMessage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrogramComponent)
};
