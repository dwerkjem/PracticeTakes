#include "TunerComponent.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr double immediatePitchJumpSemitones = 5.0;
constexpr double matchingJumpToleranceSemitones = 1.5;
constexpr int pitchJumpConfirmationFrames = 4;

constexpr std::array<const char*, 12> noteNames{
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

[[nodiscard]] juce::String noteNameForMidi(int midiNote)
{
    const auto noteIndex = ((midiNote % 12) + 12) % 12;
    const auto octave = (midiNote / 12) - 1;

    return juce::String(noteNames[static_cast<std::size_t>(noteIndex)]) + juce::String(octave);
}

} // namespace

//==============================================================================
TunerComponent::TunerComponent(
    AudioInputService& sharedAudioInputService,
    std::function<void()> feedbackHandler)
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
    feedbackButton.setTitle("Give feedback about the Tuner");
    feedbackButton.onClick = std::move(feedbackHandler);
    addAndMakeVisible(feedbackButton);

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

    for (auto* button : {&advancedSettingsButton, &clearGraphButton, &feedbackButton})
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
    if (analysis.frequency > 0.0)
    {
        handleDetectedPitch(analysis.frequency);
    }
    else
    {
        handleMissingPitch();
    }

    repaint();
}

void TunerComponent::handleDetectedPitch(double frequency)
{
    if (!isConfirmedPitch(frequency))
    {
        // Do not let a suspected harmonic alter the smoothing history or draw
        // a line to it in the graph. A real large interval will be admitted
        // after it remains stable for a few consecutive analysis frames.
        addHistoryPoint(std::numeric_limits<double>::quiet_NaN());
        return;
    }

    framesWithoutPitch = 0;

    const auto stableFrequency = smoothFrequency(frequency);
    updateDisplayedNote(stableFrequency);
    addHistoryPoint(smoothedMidiNote);
    hasSignal = true;
}

bool TunerComponent::isConfirmedPitch(double frequency)
{
    const auto midiPitch = frequencyToMidi(frequency);
    if (std::abs(smoothedMidiNote) <= std::numeric_limits<double>::epsilon() ||
        std::abs(midiPitch - smoothedMidiNote) <= immediatePitchJumpSemitones)
    {
        pendingJumpFrames = 0;
        pendingJumpMidiNote = 0.0;
        return true;
    }

    if (pendingJumpFrames == 0 ||
        std::abs(midiPitch - pendingJumpMidiNote) > matchingJumpToleranceSemitones)
    {
        pendingJumpMidiNote = midiPitch;
        pendingJumpFrames = 1;
        return false;
    }

    // Follow a genuine glide while requiring all confirming frames to remain
    // in the same small pitch neighbourhood.
    pendingJumpMidiNote +=
        (midiPitch - pendingJumpMidiNote) / static_cast<double>(pendingJumpFrames + 1);
    ++pendingJumpFrames;

    if (pendingJumpFrames < pitchJumpConfirmationFrames)
    {
        return false;
    }

    // Begin a fresh smoothing segment at the confirmed note. Otherwise the
    // old pitch would keep making the newly accepted note look like a jump.
    smoothedMidiNote = midiPitch;
    recentPitchCount = 0;
    recentPitchWriteIndex = 0;
    pendingJumpFrames = 0;
    pendingJumpMidiNote = 0.0;
    return true;
}

void TunerComponent::handleMissingPitch()
{
    pendingJumpFrames = 0;
    pendingJumpMidiNote = 0.0;
    ++framesWithoutPitch;
    addHistoryPoint(std::numeric_limits<double>::quiet_NaN());

    if (framesWithoutPitch >= static_cast<int>(dropoutSlider.getValue()))
    {
        resetPitchTracking();
    }
}

double TunerComponent::smoothFrequency(double frequency)
{
    const auto midiPitch = frequencyToMidi(frequency);

    recentMidiPitches[static_cast<std::size_t>(recentPitchWriteIndex)] = midiPitch;
    recentPitchWriteIndex = (recentPitchWriteIndex + 1) % maximumAverageWindow;
    recentPitchCount = std::min(recentPitchCount + 1, maximumAverageWindow);

    const auto averagedMidiPitch = averageRecentMidiPitches();
    const auto easing = easingSlider.getValue();

    smoothedMidiNote = std::abs(smoothedMidiNote) <= std::numeric_limits<double>::epsilon()
                           ? averagedMidiPitch
                           : smoothedMidiNote + easing * (averagedMidiPitch - smoothedMidiNote);

    return midiToFrequency(smoothedMidiNote);
}

double TunerComponent::averageRecentMidiPitches() const
{
    const auto requestedWindow = static_cast<int>(averagingSlider.getValue());
    const auto pitchesToAverage = std::min(requestedWindow, recentPitchCount);

    double sum = 0.0;
    for (int offset = 0; offset < pitchesToAverage; ++offset)
    {
        const auto index =
            (recentPitchWriteIndex - 1 - offset + maximumAverageWindow) % maximumAverageWindow;
        sum += recentMidiPitches[static_cast<std::size_t>(index)];
    }

    return sum / static_cast<double>(std::max(1, pitchesToAverage));
}

double TunerComponent::frequencyToMidi(double frequency)
{
    return 69.0 + 12.0 * std::log2(frequency / referenceFrequencyHz);
}

double TunerComponent::midiToFrequency(double midiPitch)
{
    return referenceFrequencyHz * std::pow(2.0, (midiPitch - 69.0) / 12.0);
}

void TunerComponent::updateDisplayedNote(double frequency)
{
    const auto midiPitch = frequencyToMidi(frequency);
    const auto nearestMidiNote = static_cast<int>(std::round(midiPitch));

    if (!hasLockedMidiNote)
    {
        lockedMidiNote = nearestMidiNote;
        hasLockedMidiNote = true;
    }
    else if (std::abs(midiPitch - static_cast<double>(lockedMidiNote)) > thresholdSlider.getValue())
    {
        // Keep the current note label until the pitch moves far enough away.
        // This prevents the display from rapidly switching near a boundary.
        lockedMidiNote = nearestMidiNote;
    }

    displayedNote = noteNameForMidi(lockedMidiNote);
    displayedFrequency = frequency;
    displayedCents = 100.0 * (midiPitch - static_cast<double>(lockedMidiNote));
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
    recentMidiPitches.fill(0.0);
    recentPitchCount = 0;
    recentPitchWriteIndex = 0;
    smoothedMidiNote = 0.0;
    framesWithoutPitch = 0;
    pendingJumpFrames = 0;
    pendingJumpMidiNote = 0.0;
    hasLockedMidiNote = false;
    hasSignal = false;
    displayedFrequency = 0.0;
    displayedCents = 0.0;
    displayedNote = "--";
}
