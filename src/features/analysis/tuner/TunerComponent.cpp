#include "TunerComponent.h"

#include "TunerSettingsCodec.h"

#include "application/theme/AppLookAndFeel.h"

#include <algorithm>
#include <cmath>

// The display-mode label and chooser as one component, so a docked panel adopts
// the pair with a single reparent and lays out one thing.
class TunerComponent::ModeChooser final : public juce::Component
{
  public:
    ModeChooser(juce::Label& labelToUse, juce::ComboBox& boxToUse)
        : label(labelToUse), box(boxToUse)
    {
        addAndMakeVisible(label);
        addAndMakeVisible(box);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        // The label takes what it needs and the box the rest, so this reads the
        // same on a header line as it did on a row of its own.
        label.setBounds(bounds.removeFromLeft(juce::jmin(88, bounds.getWidth() / 2)));
        bounds.removeFromLeft(6);
        box.setBounds(bounds);
    }

  private:
    juce::Label& label;
    juce::ComboBox& box;
};

#include <algorithm>
#include <cmath>
#include <limits>

// The thresholds that decide a glide from a misdetection, the smoothing, and
// the note lock all live in PitchTracker now. noteNameForMidi comes from the
// same header, so this file and TunerDrawing.cpp cannot disagree about the name
// of a note -- they held separate identical copies before.

//==============================================================================
TunerComponent::TunerComponent(AudioInputService& sharedAudioInputService)
    : audioInputService(sharedAudioInputService)
{
    setOpaque(true);

    const auto configureLabel = [this](juce::Label& label, const juce::String& text)
    {
        label.setText(text, juce::dontSendNotification);
        label.setFont(juce::FontOptions(13.0f));
        addAndMakeVisible(label);
    };

    configureLabel(displayModeLabel, "Display mode");
    configureLabel(easingLabel, "Pitch easing");
    configureLabel(averagingLabel, "Average window");
    configureLabel(thresholdLabel, "Note switch");
    configureLabel(dropoutLabel, "Dropout hold");
    configureLabel(durationLabel, "Graph duration");

    displayModeBox.addItem("Graph", static_cast<int>(DisplayMode::graph));
    displayModeBox.addItem("Bar", static_cast<int>(DisplayMode::bar));
    displayModeBox.addItem("Meter", static_cast<int>(DisplayMode::meter));
    displayModeBox.setSelectedId(AppDefaults::Tuner::displayMode, juce::dontSendNotification);
    displayModeBox.onChange = [this]
    {
        updateGraphControlAvailability();
        resized();
        repaint();
    };

    // The advanced settings live behind the panel's "..." menu rather than a
    // button of their own. Most sessions never open them, and a permanent row
    // for them was a row the display did not get.
    modeChooser = std::make_unique<ModeChooser>(displayModeLabel, displayModeBox);
    addAndMakeVisible(*modeChooser);

    configureSlider(easingSlider, 0.02, 1.0, 0.01, AppDefaults::Tuner::easing, "");
    configureSlider(averagingSlider, 1.0, 15.0, 1.0, AppDefaults::Tuner::averaging, " samples");
    configureSlider(
        thresholdSlider, 0.1, 1.5, 0.05, AppDefaults::Tuner::noteSwitchSemitones, " st");
    configureSlider(dropoutSlider, 1.0, 20.0, 1.0, AppDefaults::Tuner::dropoutFrames, " frames");
    configureSlider(
        durationSlider, 5.0, 60.0, 1.0, AppDefaults::Tuner::graphDurationSeconds, " sec");

    clearGraphButton.onClick = [this]
    {
        graphHistory.clear();
        repaint(displayBounds);
    };
    addAndMakeVisible(clearGraphButton);

    applyThemeToControls();
    updateAdvancedSettingsVisibility();

    audioInputService.addListener(this);
    startTimerHz(analysisRefreshRateHz);
}

TunerComponent::~TunerComponent()
{
    stopTimer();
    audioInputService.removeListener(this);
}

bool TunerComponent::showView(const juce::String& view)
{
    // The tuner's own vocabulary. A screenshot of the graph says nothing about
    // the bar or the meter, so each is a surface of its own.
    const auto selectMode = [this](DisplayMode mode)
    { displayModeBox.setSelectedId(static_cast<int>(mode), juce::sendNotificationSync); };

    if (view == "graph")
    {
        selectMode(DisplayMode::graph);

        return true;
    }

    if (view == "bar")
    {
        selectMode(DisplayMode::bar);

        return true;
    }

    if (view == "meter")
    {
        selectMode(DisplayMode::meter);

        return true;
    }

    if (view == "advanced")
    {
        // The display too, not only the panel. A state has to describe what is
        // on screen rather than what changed about it: without this, the
        // advanced surface inherited whichever display the previous state left
        // selected, so the same capture showed the graph or the meter
        // depending on what came before it in the run.
        selectMode(DisplayMode::graph);
        areAdvancedSettingsExpanded = true;
        updateAdvancedSettingsVisibility();
        resized();

        return true;
    }

    return false;
}

void TunerComponent::resetToDefaults()
{
    displayModeBox.setSelectedId(AppDefaults::Tuner::displayMode, juce::sendNotificationSync);
    easingSlider.setValue(AppDefaults::Tuner::easing);
    averagingSlider.setValue(AppDefaults::Tuner::averaging);
    thresholdSlider.setValue(AppDefaults::Tuner::noteSwitchSemitones);
    dropoutSlider.setValue(AppDefaults::Tuner::dropoutFrames);
    durationSlider.setValue(AppDefaults::Tuner::graphDurationSeconds);
    areAdvancedSettingsExpanded = false;
    graphHistory.clear();
    resetPitchTracking();
    updateAdvancedSettingsVisibility();
    resized();
    repaint();
}

void TunerComponent::applyPreset(AppDefaults::Preset preset)
{
    applySettings(AppDefaults::tunerPreset(preset));
}

void TunerComponent::applySettings(const AppDefaults::TunerSettings& settings)
{
    displayModeBox.setSelectedId(settings.displayMode, juce::sendNotificationSync);
    easingSlider.setValue(settings.easing);
    averagingSlider.setValue(settings.averaging);
    thresholdSlider.setValue(settings.noteSwitchSemitones);
    dropoutSlider.setValue(settings.dropoutFrames);
    durationSlider.setValue(settings.graphDurationSeconds);
    graphHistory.clear();
    resetPitchTracking();
    repaint();
}

AppDefaults::TunerSettings TunerComponent::settings() const
{
    return {displayModeBox.getSelectedId(), easingSlider.getValue(),  averagingSlider.getValue(),
            thresholdSlider.getValue(),     dropoutSlider.getValue(), durationSlider.getValue()};
}

std::optional<ToolSettingsPayload> TunerComponent::captureSettings() const
{
    return TunerSettingsCodec::encode(settings());
}

void TunerComponent::applySettings(const ToolSettingsPayload& payload)
{
    // A payload that does not parse leaves the tuner as it is rather than
    // resetting it: the workspace that carried it is refused upstream, and
    // wiping a working tuner on top of that would lose the user's settings
    // twice over.
    if (const auto decoded = TunerSettingsCodec::decode(payload); decoded.has_value())
    {
        applySettings(*decoded);
    }
}

//==============================================================================
// Appearance and control setup

void TunerComponent::setTheme(Theme theme)
{
    if (currentTheme == theme)
    {
        return;
    }

    currentTheme = theme;
    applyThemeToControls();
    repaint();
}

void TunerComponent::applyThemeToControls()
{
    const auto palette = tunerPaletteFor(currentTheme);

    for (auto* label :
         {&displayModeLabel, &easingLabel, &averagingLabel, &thresholdLabel, &dropoutLabel,
          &durationLabel})
    {
        label->setColour(juce::Label::textColourId, palette.muted);
    }

    for (auto* button : {&clearGraphButton})
    {
        button->setColour(juce::TextButton::buttonColourId, palette.control);
        button->setColour(juce::TextButton::buttonOnColourId, palette.accent.withAlpha(0.75f));
        button->setColour(juce::TextButton::textColourOffId, palette.foreground);
        button->setColour(juce::TextButton::textColourOnId, palette.foreground);
    }

    displayModeBox.setColour(juce::ComboBox::backgroundColourId, palette.control);
    displayModeBox.setColour(juce::ComboBox::textColourId, palette.foreground);
    displayModeBox.setColour(juce::ComboBox::outlineColourId, palette.outline);
    displayModeBox.setColour(juce::ComboBox::arrowColourId, palette.foreground);

    for (auto* slider :
         {&easingSlider, &averagingSlider, &thresholdSlider, &dropoutSlider, &durationSlider})
    {
        slider->setColour(juce::Slider::backgroundColourId, palette.panel);
        slider->setColour(juce::Slider::trackColourId, palette.accent.withAlpha(0.75f));
        slider->setColour(juce::Slider::thumbColourId, palette.accent);
        slider->setColour(juce::Slider::textBoxTextColourId, palette.foreground);
        slider->setColour(juce::Slider::textBoxBackgroundColourId, palette.control);
        slider->setColour(juce::Slider::textBoxOutlineColourId, palette.outline);
    }

    sendLookAndFeelChange();
}

void TunerComponent::configureSlider(
    juce::Slider& slider,
    double minimum,
    double maximum,
    double interval,
    double initialValue,
    const juce::String& suffix)
{
    slider.setRange(minimum, maximum, interval);
    slider.setValue(initialValue, juce::dontSendNotification);
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 82, 22);
    slider.setTextValueSuffix(suffix);
    addAndMakeVisible(slider);
}

void TunerComponent::updateAdvancedSettingsVisibility()
{
    for (auto* component : std::array<juce::Component*, 10>{
             &easingLabel, &easingSlider, &averagingLabel, &averagingSlider, &thresholdLabel,
             &thresholdSlider, &dropoutLabel, &dropoutSlider, &durationLabel, &durationSlider})
    {
        component->setVisible(areAdvancedSettingsExpanded);
    }

    updateGraphControlAvailability();
}

void TunerComponent::updateGraphControlAvailability()
{
    const auto isGraphSelected =
        displayModeBox.getSelectedId() == static_cast<int>(DisplayMode::graph);

    durationLabel.setEnabled(isGraphSelected);
    durationSlider.setEnabled(isGraphSelected);
    clearGraphButton.setVisible(areAdvancedSettingsExpanded && isGraphSelected);
}

int TunerComponent::controlAreaHeight() const
{
    constexpr int expandedRowsHeight = 5 * 30 + 8 + 36;

    // Nothing but the advanced rows, and only while they are open. The mode
    // chooser is on the header line when the tuner is docked, and the tuner
    // adds a row for it only when nobody adopted it -- which resized() works
    // out, since it is the only place that knows.
    return (areAdvancedSettingsExpanded ? expandedRowsHeight + 8 : 0) +
           (isModeChooserAdopted() ? 0 : modeChooserHeight + 8);
}

bool TunerComponent::isModeChooserAdopted() const
{
    // Adopted means some other component -- a docked panel's header -- took it
    // as a child. Floating, nobody does, so the tuner shows it itself.
    return modeChooser != nullptr && modeChooser->getParentComponent() != nullptr &&
           modeChooser->getParentComponent() != this;
}

void TunerComponent::placeModeChooser(juce::Rectangle<int> area)
{
    if (modeChooser == nullptr)
    {
        return;
    }

    addAndMakeVisible(*modeChooser);
    modeChooser->setBounds(area);
}

juce::Component* TunerComponent::headerControl()
{
    return modeChooser.get();
}

std::vector<ToolComponent::MenuEntry> TunerComponent::optionsMenuEntries()
{
    // The mode chooser lives on the header line -- which a pane too small to
    // show a header line does not have (DockedToolPanel hides it below the
    // compact threshold), and a floating window's own title bar has no room
    // for it either. Right-click already reaches this menu at every size and
    // presentation; without these three, that reach did not include switching
    // the one thing a tuner offers more than one view of.
    const auto currentMode = static_cast<DisplayMode>(displayModeBox.getSelectedId());
    const auto selectMode = [this](DisplayMode mode)
    { displayModeBox.setSelectedId(static_cast<int>(mode), juce::sendNotificationSync); };

    return {
        {"Graph", [selectMode] { selectMode(DisplayMode::graph); },
         currentMode == DisplayMode::graph},
        {"Bar", [selectMode] { selectMode(DisplayMode::bar); }, currentMode == DisplayMode::bar},
        {"Meter", [selectMode] { selectMode(DisplayMode::meter); },
         currentMode == DisplayMode::meter},
        {areAdvancedSettingsExpanded ? "Hide advanced settings" : "Advanced settings", [this]
         {
             areAdvancedSettingsExpanded = !areAdvancedSettingsExpanded;
             updateAdvancedSettingsVisibility();
             resized();
             repaint();
         }}};
}

juce::String TunerComponent::statusText() const
{
    if (audioErrorMessage.isNotEmpty())
    {
        return audioErrorMessage;
    }

    // Not "whenever there is no signal right now" -- a brief silent gap
    // between two notes clears `hasSignal` too, and mid-practice that is not
    // an invitation to start, it is the ordinary shape of playing something.
    // The prompt belongs to a graph that has nothing in it yet; once there is
    // a real reading in the history, a pause reads as a pause, not as an
    // empty tool asking to be used.
    if (!hasSignal && !hasGraphHistory())
    {
        return "Play or sing a sustained note";
    }

    // Blank rather than a placeholder: the status line still holds its row
    // either way (statusHeight is reserved unconditionally), and a brief gap
    // between notes has nothing worth reporting -- not silence to name, not
    // an error, just a moment where the reading is not current.
    if (!hasSignal)
    {
        return {};
    }

    // One space, not the three the design's HTML carries -- a browser collapses
    // runs of whitespace and renders exactly one, which is what the reference
    // actually shows. JUCE draws all three.
    const auto centsSign = displayedCents > 0.0 ? "+" : "";
    return juce::String(displayedFrequency, 1) + " Hz " + centsSign +
           juce::String(displayedCents, 1) + " cents";
}

bool TunerComponent::hasGraphHistory() const
{
    return std::any_of(
        graphHistory.begin(), graphHistory.end(),
        [](double value) { return std::isfinite(value); });
}

//==============================================================================
// Audio capture

void TunerComponent::audioInputAboutToStart(double sampleRate, int inputChannels)
{
    juce::ignoreUnused(inputChannels);
    currentSampleRate.store(sampleRate);
    audioInputService.discardPendingSamples(this);
    analysisBuffer.fill(0.0f);
}

void TunerComponent::audioInputStopped()
{
    audioInputService.discardPendingSamples(this);
}

void TunerComponent::audioInputStateChanged(AudioInputService::InputState state)
{
    audioInputService.discardPendingSamples(this);
    resetPitchTracking();

    switch (state)
    {
    case AudioInputService::InputState::disconnected:
        audioErrorMessage = "Microphone disconnected.";
        break;
    case AudioInputService::InputState::opening:
        audioErrorMessage = "Waiting for the microphone.";
        break;
    case AudioInputService::InputState::muted:
        audioErrorMessage = "Microphone muted.";
        break;
    case AudioInputService::InputState::clipping:
        audioErrorMessage = "Microphone input is clipping.";
        break;
    case AudioInputService::InputState::active:
        audioErrorMessage.clear();
        break;
    }

    repaint();
}

bool TunerComponent::drainAudioFifo()
{
    const auto availableSamples =
        std::min(audioInputService.availableSamples(this), drainBuffer.size());
    if (availableSamples == 0)
    {
        return false;
    }

    const auto samplesRead =
        audioInputService.readSamples(this, drainBuffer.data(), availableSamples);
    if (samplesRead == 0)
    {
        return false;
    }

    if (samplesRead >= analysisWindowSize)
    {
        // Keep only the newest complete analysis window.
        std::copy_n(
            drainBuffer.begin() + static_cast<std::ptrdiff_t>(samplesRead - analysisWindowSize),
            analysisWindowSize, analysisBuffer.begin());
        return true;
    }

    // Shift older samples left and append the newly captured samples.
    const auto sampleCount = static_cast<std::ptrdiff_t>(samplesRead);
    std::move(analysisBuffer.begin() + sampleCount, analysisBuffer.end(), analysisBuffer.begin());
    std::copy_n(drainBuffer.begin(), sampleCount, analysisBuffer.end() - sampleCount);
    return true;
}

//==============================================================================
// Pitch analysis

void TunerComponent::timerCallback()
{
    if (!drainAudioFifo())
    {
        return;
    }

    const auto analysis = pitchDetector.detect(analysisBuffer, currentSampleRate.load());
    inputLevel = analysis.inputLevel;

    const auto update =
        analysis.frequency > 0.0 ? pitchTracker.detected(analysis.frequency, trackerSettings())
                                 : pitchTracker.missing(trackerSettings());
    applyTrackerUpdate(update);

    repaint();
}

PitchTracker::Settings TunerComponent::trackerSettings() const
{
    // Read straight off the sliders each frame, so a mid-note adjustment takes
    // effect immediately rather than at the next note.
    return {
        easingSlider.getValue(),
        averagingSlider.getValue(),
        thresholdSlider.getValue(),
        dropoutSlider.getValue(),
    };
}

void TunerComponent::applyTrackerUpdate(const PitchTracker::Update& update)
{
    hasSignal = update.hasSignal;
    displayedFrequency = update.displayedFrequency;
    displayedCents = update.displayedCents;
    lockedMidiNote = update.displayedMidiNote;
    hasLockedMidiNote = update.hasDisplayedNote;
    displayedNote = update.hasDisplayedNote
                        ? juce::String(noteNameForMidi(update.displayedMidiNote))
                        : juce::String("--");

    // Every frame contributes a point. A NaN is a deliberate gap -- silence, or
    // a reading rejected as a suspected harmonic -- and the graph draws a break
    // rather than a line across it.
    addHistoryPoint(update.historyMidiNote);
}

void TunerComponent::addHistoryPoint(double midiPitch)
{
    graphHistory.push_back(midiPitch);

    const auto desiredPointCount = juce::jlimit(
        100, maximumGraphPoints,
        static_cast<int>(durationSlider.getValue() * analysisRefreshRateHz));

    const auto excessPointCount = static_cast<int>(graphHistory.size()) - desiredPointCount;
    if (excessPointCount > 0)
    {
        graphHistory.erase(graphHistory.begin(), graphHistory.begin() + excessPointCount);
    }
}

void TunerComponent::resetPitchTracking()
{
    // Two resets that used to be one. The tracker forgets what it has heard;
    // the component clears what it is showing. They were entangled before, so
    // neither could be exercised without the other.
    pitchTracker.reset();

    hasLockedMidiNote = false;
    hasSignal = false;
    displayedFrequency = 0.0;
    displayedCents = 0.0;
    displayedNote = "--";
}
