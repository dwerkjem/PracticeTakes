#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <vector>

class TunerComponent final : public juce::Component,
                               private juce::AudioIODeviceCallback,
                               private juce::ChangeListener,
                               private juce::Timer
{
public:
    explicit TunerComponent(juce::AudioDeviceManager& sharedAudioDeviceManager);
    ~TunerComponent() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void setDarkMode(bool shouldUseDarkMode);

private:
    enum class DisplayMode
    {
        graph = 1,
        bar,
        meter
    };

    static constexpr int fifoCapacity = 65536;
    static constexpr int analysisSize = 4096;
    static constexpr int maximumAverageWindow = 15;
    static constexpr int maximumGraphPoints = 1200;

    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;

    [[nodiscard]] bool hasUsableInputDevice() const;
    void updateAudioDeviceStatus();
    void attachAudioCallbackIfPossible();
    void detachAudioCallback();
    void drainAudioFifo();
    [[nodiscard]] double detectPitch() const;
    [[nodiscard]] double smoothFrequency(double frequency);
    void resetPitchTracking();
    void updateNote(double frequency);
    void addHistoryPoint(double midiPitch);
    void configureSlider(juce::Slider& slider,
                         double minimum,
                         double maximum,
                         double interval,
                         double initialValue,
                         const juce::String& suffix);
    void updateAdvancedSettingsVisibility();
    void updateGraphControlAvailability();
    void applyThemeToControls();
    void drawPitchGraph(juce::Graphics& graphics,
                        juce::Rectangle<int> bounds) const;
    void drawPitchBar(juce::Graphics& graphics,
                      juce::Rectangle<int> bounds) const;
    void drawPitchMeter(juce::Graphics& graphics,
                        juce::Rectangle<int> bounds) const;
    void drawSelectedDisplay(juce::Graphics& graphics,
                             juce::Rectangle<int> bounds) const;

    juce::AudioDeviceManager& audioDeviceManager;
    juce::Label displayModeLabel;
    juce::ComboBox displayModeBox;
    juce::TextButton advancedSettingsButton { "Advanced settings  >" };
    juce::Label easingLabel;
    juce::Label averagingLabel;
    juce::Label thresholdLabel;
    juce::Label dropoutLabel;
    juce::Label durationLabel;
    juce::Slider easingSlider;
    juce::Slider averagingSlider;
    juce::Slider thresholdSlider;
    juce::Slider dropoutSlider;
    juce::Slider durationSlider;
    juce::TextButton clearGraphButton { "Clear graph" };

    juce::AbstractFifo audioFifo { fifoCapacity };
    std::array<float, fifoCapacity> fifoBuffer {};
    std::array<float, analysisSize> analysisBuffer {};
    std::array<double, maximumAverageWindow> pitchAverageHistory {};
    std::vector<double> graphHistory;

    std::atomic<double> currentSampleRate { 44100.0 };
    double smoothedMidiNote = 0.0;
    double displayedFrequency = 0.0;
    double displayedCents = 0.0;
    juce::String displayedNote { "--" };
    float inputLevel = 0.0f;
    int pitchHistoryCount = 0;
    int pitchHistoryWriteIndex = 0;
    int lockedMidiNote = 69;
    int framesWithoutPitch = 0;
    bool hasLockedMidiNote = false;
    bool hasSignal = false;
    bool audioCallbackAttached = false;
    bool advancedSettingsExpanded = false;
    bool darkMode = false;
    juce::String audioErrorMessage;
    juce::Rectangle<int> displayBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TunerComponent)
};
