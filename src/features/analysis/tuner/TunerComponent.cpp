#include "TunerComponent.h"

#include "TunerSettingsCodec.h"

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

    configureLabel(displayModeLabel, "Display");
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
    addAndMakeVisible(displayModeBox);

    advancedSettingsButton.onClick = [this]
    {
        areAdvancedSettingsExpanded = !areAdvancedSettingsExpanded;
        updateAdvancedSettingsVisibility();
        resized();
        repaint();
    };
    addAndMakeVisible(advancedSettingsButton);

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

    for (auto* button : {&advancedSettingsButton, &clearGraphButton})
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
    advancedSettingsButton.setButtonText(
        areAdvancedSettingsExpanded ? "Advanced settings  v" : "Advanced settings  >");

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
    constexpr int displaySelectorHeight = 32;
    constexpr int gapBelowDisplaySelector = 8;
    constexpr int advancedButtonHeight = 34;
    constexpr int expandedRowsHeight = 5 * 30 + 8 + 36;

    return displaySelectorHeight + gapBelowDisplaySelector + advancedButtonHeight +
           (areAdvancedSettingsExpanded ? expandedRowsHeight : 0);
}

juce::String TunerComponent::statusText() const
{
    if (audioErrorMessage.isNotEmpty())
    {
        return audioErrorMessage;
    }

    if (!hasSignal)
    {
        return "Play or sing a sustained note";
    }

    const auto centsSign = displayedCents > 0.0 ? "+" : "";
    return juce::String(displayedFrequency, 1) + " Hz   " + centsSign +
           juce::String(displayedCents, 1) + " cents";
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
