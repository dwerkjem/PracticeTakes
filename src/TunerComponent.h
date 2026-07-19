#pragma once

#include <JuceHeader.h>

#include "Theme.h"

#include <array>
#include <atomic>
#include <vector>

// TunerComponent captures microphone samples, estimates their fundamental
// frequency, smooths the result, and renders it in one of three display modes.
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
    void setTheme(Theme theme);

  private:
    enum class DisplayMode
    {
        graph = 1,
        bar,
        meter
    };

    static constexpr int fifoCapacity = 65536;
    static constexpr int analysisWindowSize = 4096;
    static constexpr int maximumAverageWindow = 15;
    static constexpr int maximumGraphPoints = 1200;
    static constexpr int analysisRefreshRateHz = 20;
    static constexpr double referenceFrequencyHz = 440.0;

    // Audio capture ---------------------------------------------------------
    void
    audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                     float* const* outputChannelData, int numOutputChannels,
                                     int numSamples,
                                     const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    [[nodiscard]] bool hasUsableInputDevice() const;
    void updateAudioDeviceStatus();
    void attachAudioCallbackIfPossible();
    void detachAudioCallback();
    void writeInputSamplesToFifo(const float* inputSamples, int numSamples);
    void drainAudioFifo();

    // Pitch analysis --------------------------------------------------------
    void timerCallback() override;
    [[nodiscard]] float calculateInputLevel() const;
    [[nodiscard]] double detectPitch() const;
    [[nodiscard]] double smoothFrequency(double frequency);
    [[nodiscard]] bool isConfirmedPitch(double frequency);
    [[nodiscard]] double averageRecentMidiPitches() const;
    [[nodiscard]] static double frequencyToMidi(double frequency);
    [[nodiscard]] static double midiToFrequency(double midiPitch);

    void handleDetectedPitch(double frequency);
    void handleMissingPitch();
    void resetPitchTracking();
    void updateDisplayedNote(double frequency);
    void addHistoryPoint(double midiPitch);

    // Controls and appearance ----------------------------------------------
    void configureSlider(juce::Slider& slider, double minimum, double maximum, double interval,
                         double initialValue, const juce::String& suffix);
    void updateAdvancedSettingsVisibility();
    void updateGraphControlAvailability();
    void applyThemeToControls();
    [[nodiscard]] int controlAreaHeight() const;
    [[nodiscard]] juce::String statusText() const;

    // Drawing ---------------------------------------------------------------
    void drawPitchGraph(juce::Graphics& graphics, juce::Rectangle<int> bounds) const;
    void drawPitchBar(juce::Graphics& graphics, juce::Rectangle<int> bounds) const;
    void drawPitchMeter(juce::Graphics& graphics, juce::Rectangle<int> bounds) const;
    void drawSelectedDisplay(juce::Graphics& graphics, juce::Rectangle<int> bounds) const;

    juce::AudioDeviceManager& audioDeviceManager;

    juce::Label displayModeLabel;
    juce::ComboBox displayModeBox;
    juce::TextButton advancedSettingsButton{"Advanced settings  >"};
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
    juce::TextButton clearGraphButton{"Clear graph"};

    // Audio arrives on the device thread and is consumed on the UI timer.
    juce::AbstractFifo audioFifo{fifoCapacity};
    std::array<float, fifoCapacity> fifoBuffer{};
    std::array<float, analysisWindowSize> analysisBuffer{};

    // A short circular history stabilizes the pitch before display easing.
    std::array<double, maximumAverageWindow> recentMidiPitches{};
    int recentPitchCount = 0;
    int recentPitchWriteIndex = 0;

    std::vector<double> graphHistory;
    std::atomic<double> currentSampleRate{44100.0};

    double smoothedMidiNote = 0.0;
    double displayedFrequency = 0.0;
    double displayedCents = 0.0;
    juce::String displayedNote{"--"};
    float inputLevel = 0.0f;
    int lockedMidiNote = 69;
    int framesWithoutPitch = 0;
    double pendingJumpMidiNote = 0.0;
    int pendingJumpFrames = 0;

    bool hasLockedMidiNote = false;
    bool hasSignal = false;
    bool isAudioCallbackAttached = false;
    bool areAdvancedSettingsExpanded = false;
    Theme currentTheme = Theme::light;

    juce::String audioErrorMessage;
    juce::Rectangle<int> displayBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TunerComponent)
};
