#pragma once

#include <JuceHeader.h>

#include "AudioInputService.h"
#include "Theme.h"

#include <array>
#include <atomic>

// SpectrogramComponent converts microphone audio into a scrolling frequency
// image. Audio capture and FFT processing are deliberately kept separate.
class SpectrogramComponent final : public juce::Component,
                                   private AudioInputService::Listener,
                                   private juce::Timer
{
  public:
    explicit SpectrogramComponent(AudioInputService& sharedAudioInputService);
    ~SpectrogramComponent() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void setTheme(Theme theme);
    void resetToDefaults();

  private:
    static constexpr int fifoCapacity = 65536;
    static constexpr int fftOrder = 10;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int imageWidth = 720;
    static constexpr int imageHeight = 360;
    static constexpr int refreshRateHz = 30;

    static constexpr double minimumDisplayedFrequencyHz = 40.0;
    static constexpr double maximumDisplayedFrequencyHz = 20000.0;
    static constexpr float minimumAnalysisDecibels = -100.0f;
    static constexpr float visibleDecibelFloor = -90.0f;

    // Audio capture ---------------------------------------------------------
    void audioInputReceived(const float* inputSamples, int numSamples) override;
    void audioInputAboutToStart(double sampleRate) override;
    void audioInputStopped() override;
    void audioInputStateChanged(bool isAvailable) override;
    void writeInputSamplesToFifo(const float* inputSamples, int numSamples);

    // FFT and image generation ---------------------------------------------
    void timerCallback() override;
    void calculateNextColumn();
    void updateSpectrogramColumn();
    [[nodiscard]] double maximumVisibleFrequency() const;
    [[nodiscard]] double frequencyForImageRow(int imageRow) const;
    [[nodiscard]] int fftBinForFrequency(double frequency) const;
    [[nodiscard]] juce::Colour colourForLevel(float level) const;

    // Drawing and appearance ------------------------------------------------
    [[nodiscard]] float yForFrequency(double frequency) const;
    [[nodiscard]] juce::String frequencyLabel(double frequency) const;
    void drawFrequencyGrid(juce::Graphics& graphics) const;
    [[nodiscard]] juce::Colour backgroundColour() const;
    [[nodiscard]] juce::Colour panelColour() const;
    [[nodiscard]] juce::Colour mutedColour() const;
    [[nodiscard]] juce::Colour outlineColour() const;

    AudioInputService& audioInputService;
    juce::Rectangle<int> spectrogramBounds;

    // Samples are written on the audio thread and read on the timer thread.
    juce::AbstractFifo audioFifo{fifoCapacity};
    std::array<float, fifoCapacity> fifoBuffer{};
    std::array<float, fftSize * 2> fftData{};

    juce::dsp::FFT forwardFFT{fftOrder};
    juce::dsp::WindowingFunction<float> hannWindow{fftSize,
                                                   juce::dsp::WindowingFunction<float>::hann};
    juce::Image spectrogramImage{juce::Image::RGB, imageWidth, imageHeight, true};

    std::atomic<double> currentSampleRate{44100.0};
    Theme currentTheme = Theme::light;
    juce::String audioErrorMessage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrogramComponent)
};
