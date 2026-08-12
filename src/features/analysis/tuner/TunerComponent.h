#pragma once

#include <JuceHeader.h>

#include "../../../application/configuration/AppDefaults.h"
#include "../../../application/theme/Theme.h"
#include "../../../application/tools/CompactPresentation.h"
#include "../../../application/tools/ToolComponent.h"
#include "../../../platform/audio/AudioInputService.h"
#include "PitchDetector.h"
#include "PitchTracker.h"

#include <array>
#include <atomic>
#include <memory>
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
    [[nodiscard]] juce::Component* headerControl() override;
    [[nodiscard]] std::vector<MenuEntry> optionsMenuEntries() override;

  private:
    // The display-mode label and chooser, in one component so a panel can adopt
    // the pair with a single reparent. Which display the tuner is drawing is a
    // property of the tool rather than of what it is currently showing, so it
    // belongs on the panel's header line -- and a row of its own at the bottom
    // of the panel was a row the graph did not get.
    class ModeChooser;
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

    // paint() and resized() each walk the same vertical layout and have to
    // agree about what sits above the display. Named, because the two used to
    // carry the same magic 142 independently and nothing would have caught them
    // drifting apart except noticing the controls had slipped.
    static constexpr int statusHeight = 22;
    static constexpr int statusGap = 6;
    static constexpr int modeChooserHeight = 32;

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
    [[nodiscard]] bool isModeChooserAdopted() const;
    // Defined beside ModeChooser, which is only complete in TunerComponent.cpp.
    void placeModeChooser(juce::Rectangle<int> area);
    [[nodiscard]] juce::String statusText() const;

    // Drawing ---------------------------------------------------------------
    void drawPitchGraph(juce::Graphics& graphics, juce::Rectangle<int> bounds) const;
    void drawPitchBar(juce::Graphics& graphics, juce::Rectangle<int> bounds) const;
    void drawPitchMeter(juce::Graphics& graphics, juce::Rectangle<int> bounds) const;
    void drawNoteWatermark(juce::Graphics& graphics, juce::Rectangle<int> area) const;
    void drawCompactDisplay(
        juce::Graphics& graphics,
        juce::Rectangle<int> bounds,
        compact::Shape shape) const;
    void drawCompactGraph(
        juce::Graphics& graphics,
        juce::Rectangle<int> bounds,
        compact::Shape shape) const;
    void drawCompactBar(juce::Graphics& graphics, juce::Rectangle<int> bounds, compact::Shape shape)
        const;
    void drawCompactMeter(
        juce::Graphics& graphics,
        juce::Rectangle<int> bounds,
        compact::Shape shape) const;
    void drawCompactReading(juce::Graphics& graphics, juce::Rectangle<int> area) const;
    [[nodiscard]] juce::String directionLabel() const;
    void drawSelectedDisplay(juce::Graphics& graphics, juce::Rectangle<int> bounds) const;

    AudioInputService& audioInputService;

    juce::Label displayModeLabel;
    juce::ComboBox displayModeBox;

    // Owned here, but reparented into the docked panel's header when there is
    // one. Declared after the two controls it lays out, so it is destroyed
    // first and never lays out a dangling reference.
    std::unique_ptr<ModeChooser> modeChooser;

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
