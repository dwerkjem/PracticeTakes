#pragma once

#include <JuceHeader.h>

#include "../../../application/configuration/AppDefaults.h"
#include "../../../application/theme/Theme.h"
#include "../../../application/tools/ToolComponent.h"
#include "../../../platform/audio/AudioInputService.h"
#include "PitchDetector.h"
#include "PitchTracker.h"

#include <array>
#include <atomic>
#include <optional>
#include <vector>

// TunerComponent captures microphone samples, estimates their fundamental
// frequency, smooths the result, and renders it in one of three display modes.
class TunerComponent final
    : public ToolComponent,
      private AudioInputService::Listener,
      private juce::Timer
{
  public:
    explicit TunerComponent(AudioInputService& sharedAudioInputService);
    ~TunerComponent() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void setTheme(Theme theme) override;
    void resetToDefaults() override;
    void applyPreset(AppDefaults::Preset preset);

    // The typed pair the settings window and presets use directly, alongside
    // the opaque pair the shell uses to move settings into a workspace.
    void applySettings(const AppDefaults::TunerSettings& settings);
    [[nodiscard]] AppDefaults::TunerSettings settings() const;

    [[nodiscard]] std::optional<ToolSettingsPayload> captureSettings() const override;
    void applySettings(const ToolSettingsPayload& payload) override;
    [[nodiscard]] bool showView(const juce::String& view) override;

  private:
    enum class DisplayMode
    {
        graph = 1,
        bar,
        meter
    };

    static constexpr int fifoCapacity = 65536;
    static constexpr int analysisWindowSize = PitchDetector::windowSize;
    static constexpr int maximumGraphPoints = 1200;
    static constexpr int analysisRefreshRateHz = 20;

    // Audio capture ---------------------------------------------------------
    void audioInputAboutToStart(double sampleRate, int inputChannels) override;
    void audioInputStopped() override;
    void audioInputStateChanged(AudioInputService::InputState state) override;
    [[nodiscard]] bool drainAudioFifo();

    // Pitch analysis --------------------------------------------------------
    // The tracking itself lives in PitchTracker, which is JUCE-free and tested
    // directly. What remains here is turning one of its updates into the
    // widgets' state and the graph.
    void timerCallback() override;
    [[nodiscard]] PitchTracker::Settings trackerSettings() const;
    void applyTrackerUpdate(const PitchTracker::Update& update);
    void addHistoryPoint(double midiPitch);
    void resetPitchTracking();

    // Controls and appearance ----------------------------------------------
    void configureSlider(
        juce::Slider& slider,
        double minimum,
        double maximum,
        double interval,
        double initialValue,
        const juce::String& suffix);
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

    AudioInputService& audioInputService;

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

    // The shared service fills this tool's bounded FIFO. The timer drains it
    // into preallocated storage before analysis.
    std::array<float, fifoCapacity> drainBuffer{};
    std::array<float, analysisWindowSize> analysisBuffer{};
    PitchDetector pitchDetector;
    PitchTracker pitchTracker;

    std::vector<double> graphHistory;
    std::atomic<double> currentSampleRate{44100.0};

    // Mirrors of the tracker's latest update. They are members because the
    // drawing code in TunerDrawing.cpp reads them directly; nothing here
    // computes them.
    double displayedFrequency = 0.0;
    double displayedCents = 0.0;
    juce::String displayedNote{"--"};
    float inputLevel = 0.0f;
    int lockedMidiNote = 69;

    bool hasLockedMidiNote = false;
    bool hasSignal = false;
    bool areAdvancedSettingsExpanded = false;
    Theme currentTheme = Theme::light;

    juce::String audioErrorMessage;
    juce::Rectangle<int> displayBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TunerComponent)
};
