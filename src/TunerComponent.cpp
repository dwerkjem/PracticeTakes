#include "TunerComponent.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace
{
constexpr double minimumDetectableFrequencyHz = 16.352;
constexpr double maximumDetectableFrequencyHz = 2500.0;
constexpr float minimumInputRms = 0.008f;
constexpr double minimumCorrelationScore = 0.72;
constexpr double inTuneToleranceCents = 5.0;
constexpr double immediatePitchJumpSemitones = 5.0;
constexpr double matchingJumpToleranceSemitones = 1.5;
constexpr int pitchJumpConfirmationFrames = 4;

struct TunerPalette
{
    juce::Colour background;
    juce::Colour panel;
    juce::Colour control;
    juce::Colour outline;
    juce::Colour foreground;
    juce::Colour muted;
    juce::Colour accent;
    juce::Colour inTune;
};

[[nodiscard]] TunerPalette tunerPaletteFor(bool isDarkMode)
{
    if (isDarkMode)
    {
        return {juce::Colour::fromRGB(18, 20, 27),    juce::Colour::fromRGB(25, 28, 37),
                juce::Colour::fromRGB(54, 59, 72),    juce::Colour::fromRGB(58, 65, 82),
                juce::Colour::fromRGB(238, 241, 247), juce::Colour::fromRGB(142, 150, 166),
                juce::Colour::fromRGB(100, 170, 255), juce::Colour::fromRGB(85, 214, 136)};
    }

    return {juce::Colour::fromRGB(235, 236, 238), juce::Colour::fromRGB(250, 250, 251),
            juce::Colour::fromRGB(220, 224, 230), juce::Colour::fromRGB(165, 169, 178),
            juce::Colour::fromRGB(28, 31, 37),    juce::Colour::fromRGB(92, 98, 108),
            juce::Colour::fromRGB(55, 112, 196),  juce::Colour::fromRGB(35, 145, 82)};
}

constexpr std::array<const char*, 12> noteNames{"C",  "C#", "D",  "D#", "E",  "F",
                                                "F#", "G",  "G#", "A",  "A#", "B"};

[[nodiscard]] juce::String noteNameForMidi(int midiNote)
{
    const auto noteIndex = ((midiNote % 12) + 12) % 12;
    const auto octave = (midiNote / 12) - 1;

    return juce::String(noteNames[static_cast<std::size_t>(noteIndex)]) + juce::String(octave);
}

void clearOutputChannels(float* const* outputChannelData, int numOutputChannels, int numSamples)
{
    for (int channel = 0; channel < numOutputChannels; ++channel)
    {
        if (outputChannelData[channel] != nullptr)
        {
            juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);
        }
    }
}
} // namespace

//==============================================================================
TunerComponent::TunerComponent(juce::AudioDeviceManager& sharedAudioDeviceManager)
    : audioDeviceManager(sharedAudioDeviceManager)
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
    displayModeBox.setSelectedId(static_cast<int>(DisplayMode::graph), juce::dontSendNotification);
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

    configureSlider(easingSlider, 0.02, 1.0, 0.01, 0.35, "");
    configureSlider(averagingSlider, 1.0, 15.0, 1.0, 5.0, " samples");
    configureSlider(thresholdSlider, 0.1, 1.5, 0.05, 0.55, " st");
    configureSlider(dropoutSlider, 1.0, 20.0, 1.0, 4.0, " frames");
    configureSlider(durationSlider, 5.0, 60.0, 1.0, 20.0, " sec");

    clearGraphButton.onClick = [this]
    {
        graphHistory.clear();
        repaint(displayBounds);
    };
    addAndMakeVisible(clearGraphButton);

    applyThemeToControls();
    updateAdvancedSettingsVisibility();

    audioDeviceManager.addChangeListener(this);
    updateAudioDeviceStatus();
    startTimerHz(analysisRefreshRateHz);
}

TunerComponent::~TunerComponent()
{
    stopTimer();
    audioDeviceManager.removeChangeListener(this);
    detachAudioCallback();
}

//==============================================================================
// Appearance and control setup

void TunerComponent::setDarkMode(bool shouldUseDarkMode)
{
    if (isDarkMode == shouldUseDarkMode)
    {
        return;
    }

    isDarkMode = shouldUseDarkMode;
    applyThemeToControls();
    repaint();
}

void TunerComponent::applyThemeToControls()
{
    const auto palette = tunerPaletteFor(isDarkMode);

    for (auto* label : {&displayModeLabel, &easingLabel, &averagingLabel, &thresholdLabel,
                        &dropoutLabel, &durationLabel})
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

void TunerComponent::configureSlider(juce::Slider& slider, double minimum, double maximum,
                                     double interval, double initialValue,
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
    advancedSettingsButton.setButtonText(areAdvancedSettingsExpanded ? "Advanced settings  v"
                                                                     : "Advanced settings  >");

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

void TunerComponent::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                      int numInputChannels,
                                                      float* const* outputChannelData,
                                                      int numOutputChannels, int numSamples,
                                                      const juce::AudioIODeviceCallbackContext&)
{
    // Practice Takes currently analyzes input only. Always clear any output
    // buffers supplied by the device manager to avoid accidental feedback.
    clearOutputChannels(outputChannelData, numOutputChannels, numSamples);

    if (numInputChannels <= 0 || inputChannelData[0] == nullptr)
    {
        return;
    }

    writeInputSamplesToFifo(inputChannelData[0], numSamples);
}

void TunerComponent::writeInputSamplesToFifo(const float* inputSamples, int numSamples)
{
    const auto writableSamples = std::min(numSamples, audioFifo.getFreeSpace());
    const auto writeScope = audioFifo.write(writableSamples);

    std::copy_n(inputSamples, writeScope.blockSize1, fifoBuffer.begin() + writeScope.startIndex1);
    std::copy_n(inputSamples + writeScope.blockSize1, writeScope.blockSize2,
                fifoBuffer.begin() + writeScope.startIndex2);
}

void TunerComponent::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    currentSampleRate.store(device != nullptr ? device->getCurrentSampleRate() : 44100.0);
    audioFifo.reset();
    analysisBuffer.fill(0.0f);
}

void TunerComponent::audioDeviceStopped()
{
    audioFifo.reset();
}

void TunerComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &audioDeviceManager)
    {
        updateAudioDeviceStatus();
    }
}

bool TunerComponent::hasUsableInputDevice() const
{
    auto* device = audioDeviceManager.getCurrentAudioDevice();

    return device != nullptr && device->isOpen() &&
           device->getActiveInputChannels().countNumberOfSetBits() > 0;
}

void TunerComponent::attachAudioCallbackIfPossible()
{
    if (isAudioCallbackAttached || !hasUsableInputDevice())
    {
        return;
    }

    audioDeviceManager.addAudioCallback(this);
    isAudioCallbackAttached = true;
}

void TunerComponent::detachAudioCallback()
{
    if (!isAudioCallbackAttached)
    {
        return;
    }

    audioDeviceManager.removeAudioCallback(this);
    isAudioCallbackAttached = false;
}

void TunerComponent::updateAudioDeviceStatus()
{
    if (hasUsableInputDevice())
    {
        audioErrorMessage.clear();
        attachAudioCallbackIfPossible();
    }
    else
    {
        detachAudioCallback();
        audioErrorMessage = "No microphone input is available.";
        resetPitchTracking();
    }

    repaint();
}

void TunerComponent::drainAudioFifo()
{
    const auto availableSamples = audioFifo.getNumReady();
    if (availableSamples <= 0)
    {
        return;
    }

    std::vector<float> newSamples(static_cast<std::size_t>(availableSamples));
    const auto readScope = audioFifo.read(availableSamples);

    std::copy_n(fifoBuffer.begin() + readScope.startIndex1, readScope.blockSize1,
                newSamples.begin());
    std::copy_n(fifoBuffer.begin() + readScope.startIndex2, readScope.blockSize2,
                newSamples.begin() + readScope.blockSize1);

    if (availableSamples >= analysisWindowSize)
    {
        // Keep only the newest complete analysis window.
        std::copy(newSamples.end() - analysisWindowSize, newSamples.end(), analysisBuffer.begin());
        return;
    }

    // Shift older samples left and append the newly captured samples.
    std::move(analysisBuffer.begin() + availableSamples, analysisBuffer.end(),
              analysisBuffer.begin());
    std::copy(newSamples.begin(), newSamples.end(), analysisBuffer.end() - availableSamples);
}

//==============================================================================
// Pitch analysis

void TunerComponent::timerCallback()
{
    drainAudioFifo();
    inputLevel = calculateInputLevel();

    const auto detectedFrequency = detectPitch();
    if (detectedFrequency > 0.0)
    {
        handleDetectedPitch(detectedFrequency);
    }
    else
    {
        handleMissingPitch();
    }

    repaint();
}

float TunerComponent::calculateInputLevel() const
{
    const auto squareSum = std::inner_product(analysisBuffer.begin(), analysisBuffer.end(),
                                              analysisBuffer.begin(), 0.0);

    return static_cast<float>(std::sqrt(squareSum / static_cast<double>(analysisWindowSize)));
}

double TunerComponent::detectPitch() const
{
    const auto sampleRate = currentSampleRate.load();
    if (inputLevel < minimumInputRms || sampleRate <= 0.0)
    {
        return 0.0;
    }

    // Autocorrelation compares the analysis window with delayed copies of
    // itself. The delay with the strongest similarity represents one period.
    const auto minimumLag =
        std::max(2, static_cast<int>(sampleRate / maximumDetectableFrequencyHz));
    const auto maximumLag = std::min(analysisWindowSize / 2,
                                     static_cast<int>(sampleRate / minimumDetectableFrequencyHz));

    std::array<double, (analysisWindowSize / 2) + 1> correlations{};

    for (int lag = minimumLag; lag <= maximumLag; ++lag)
    {
        double numerator = 0.0;
        double firstEnergy = 0.0;
        double secondEnergy = 0.0;

        for (int index = 0; index < analysisWindowSize - lag; ++index)
        {
            const auto firstSample =
                static_cast<double>(analysisBuffer[static_cast<std::size_t>(index)]);
            const auto delayedSample =
                static_cast<double>(analysisBuffer[static_cast<std::size_t>(index + lag)]);

            numerator += firstSample * delayedSample;
            firstEnergy += firstSample * firstSample;
            secondEnergy += delayedSample * delayedSample;
        }

        const auto denominator = std::sqrt(firstEnergy * secondEnergy);
        correlations[static_cast<std::size_t>(lag)] =
            denominator > 0.0 ? numerator / denominator : 0.0;
    }

    // The very short lags at the start of an autocorrelation curve are often
    // highly correlated simply because adjacent audio samples are similar.
    // Treating that boundary value as a period produces spurious notes near
    // the upper detection limit. A real period is represented by a local peak
    // after the initial correlation slope has fallen and risen again.
    int periodLag = 0;
    for (int lag = minimumLag + 1; lag < maximumLag; ++lag)
    {
        const auto correlation = correlations[static_cast<std::size_t>(lag)];
        if (correlation >= minimumCorrelationScore &&
            correlation > correlations[static_cast<std::size_t>(lag - 1)] &&
            correlation >= correlations[static_cast<std::size_t>(lag + 1)])
        {
            periodLag = lag;
            break;
        }
    }

    if (periodLag == 0)
    {
        return 0.0;
    }

    // Parabolic interpolation reduces quantisation jitter without allowing a
    // non-peak boundary lag back into the result.
    const auto left = correlations[static_cast<std::size_t>(periodLag - 1)];
    const auto centre = correlations[static_cast<std::size_t>(periodLag)];
    const auto right = correlations[static_cast<std::size_t>(periodLag + 1)];
    const auto curvature = left - (2.0 * centre) + right;
    const auto offset = std::abs(curvature) > std::numeric_limits<double>::epsilon()
                            ? 0.5 * (left - right) / curvature
                            : 0.0;
    const auto refinedLag = static_cast<double>(periodLag) + juce::jlimit(-0.5, 0.5, offset);

    return sampleRate / refinedLag;
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

    const auto desiredPointCount =
        juce::jlimit(100, maximumGraphPoints,
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

//==============================================================================
// Drawing

void TunerComponent::drawPitchGraph(juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    const auto palette = tunerPaletteFor(isDarkMode);

    graphics.setColour(palette.panel);
    graphics.fillRoundedRectangle(bounds.toFloat(), 8.0f);
    graphics.setColour(palette.outline);
    graphics.drawRoundedRectangle(bounds.toFloat(), 8.0f, 1.0f);

    auto content = bounds.reduced(8);
    auto labelArea = content.removeFromLeft(48);
    const auto plotArea = content;

    std::vector<double> validValues;
    validValues.reserve(graphHistory.size());
    for (const auto value : graphHistory)
    {
        if (std::isfinite(value))
        {
            validValues.push_back(value);
        }
    }

    double minimumValue = hasLockedMidiNote ? static_cast<double>(lockedMidiNote) - 3.0 : 66.0;
    double maximumValue = hasLockedMidiNote ? static_cast<double>(lockedMidiNote) + 3.0 : 72.0;

    if (!validValues.empty())
    {
        minimumValue = *std::min_element(validValues.begin(), validValues.end()) - 0.5;
        maximumValue = *std::max_element(validValues.begin(), validValues.end()) + 0.5;

        if (maximumValue - minimumValue < 6.0)
        {
            const auto centre = (minimumValue + maximumValue) * 0.5;
            minimumValue = centre - 3.0;
            maximumValue = centre + 3.0;
        }
    }

    const auto firstNote = static_cast<int>(std::ceil(minimumValue));
    const auto lastNote = static_cast<int>(std::floor(maximumValue));
    const auto pixelsPerSemitone =
        static_cast<double>(plotArea.getHeight()) / std::max(1.0, maximumValue - minimumValue);

    graphics.setFont(juce::FontOptions(11.0f));

    for (int midiNote = firstNote; midiNote <= lastNote; ++midiNote)
    {
        const auto y =
            juce::jmap(static_cast<float>(midiNote), static_cast<float>(minimumValue),
                       static_cast<float>(maximumValue), static_cast<float>(plotArea.getBottom()),
                       static_cast<float>(plotArea.getY()));

        const auto isCurrentNote = hasSignal && midiNote == lockedMidiNote;
        const auto isC = ((midiNote % 12) + 12) % 12 == 0;

        graphics.setColour(isCurrentNote ? palette.accent.withAlpha(0.48f)
                           : isC         ? palette.muted.withAlpha(0.30f)
                                         : palette.outline.withAlpha(0.62f));
        graphics.drawHorizontalLine(static_cast<int>(std::round(y)),
                                    static_cast<float>(plotArea.getX()),
                                    static_cast<float>(plotArea.getRight()));

        if (pixelsPerSemitone >= 10.0 || isCurrentNote || isC)
        {
            graphics.setColour(isCurrentNote ? palette.accent : palette.muted);
            graphics.drawFittedText(
                noteNameForMidi(midiNote),
                labelArea.withY(static_cast<int>(std::round(y)) - 8).withHeight(16),
                juce::Justification::centredRight, 1);
        }
    }

    if (graphHistory.size() < 2 || validValues.empty())
    {
        return;
    }

    juce::Path pitchPath;
    bool pathHasStarted = false;

    for (std::size_t index = 0; index < graphHistory.size(); ++index)
    {
        const auto value = graphHistory[index];
        if (!std::isfinite(value))
        {
            pathHasStarted = false;
            continue;
        }

        const auto x = juce::jmap(
            static_cast<float>(index), 0.0f, static_cast<float>(graphHistory.size() - 1),
            static_cast<float>(plotArea.getX()), static_cast<float>(plotArea.getRight()));
        const auto y =
            juce::jmap(static_cast<float>(value), static_cast<float>(minimumValue),
                       static_cast<float>(maximumValue), static_cast<float>(plotArea.getBottom()),
                       static_cast<float>(plotArea.getY()));

        if (pathHasStarted)
        {
            pitchPath.lineTo(x, y);
        }
        else
        {
            pitchPath.startNewSubPath(x, y);
            pathHasStarted = true;
        }
    }

    graphics.setColour(palette.accent);
    graphics.strokePath(pitchPath, juce::PathStrokeType(2.0f));
}

void TunerComponent::drawPitchBar(juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    const auto palette = tunerPaletteFor(isDarkMode);
    const auto indicatorColour =
        std::abs(displayedCents) <= inTuneToleranceCents ? palette.inTune : palette.accent;

    graphics.setColour(palette.panel);
    graphics.fillRoundedRectangle(bounds.toFloat(), 8.0f);
    graphics.setColour(palette.outline);
    graphics.drawRoundedRectangle(bounds.toFloat(), 8.0f, 1.0f);

    auto barBounds = bounds.reduced(28, 34);
    const auto centreY = barBounds.getCentreY();

    graphics.setColour(palette.control);
    graphics.fillRoundedRectangle(
        barBounds.toFloat().withHeight(10.0f).withCentre(
            {static_cast<float>(barBounds.getCentreX()), static_cast<float>(centreY)}),
        5.0f);

    graphics.setFont(juce::FontOptions(12.0f));
    for (const auto cents : {-50, -25, 0, 25, 50})
    {
        const auto x = juce::jmap(static_cast<float>(cents), -50.0f, 50.0f,
                                  static_cast<float>(barBounds.getX()),
                                  static_cast<float>(barBounds.getRight()));

        graphics.setColour(cents == 0 ? palette.foreground : palette.muted);
        graphics.drawVerticalLine(static_cast<int>(std::round(x)),
                                  static_cast<float>(centreY) - 18.0f,
                                  static_cast<float>(centreY) + 18.0f);
        graphics.drawText(juce::String(cents > 0 ? "+" : "") + juce::String(cents),
                          static_cast<int>(x) - 24, centreY + 22, 48, 18,
                          juce::Justification::centred);
    }

    if (!hasSignal)
    {
        return;
    }

    const auto indicatorX =
        juce::jmap(static_cast<float>(juce::jlimit(-50.0, 50.0, displayedCents)), -50.0f, 50.0f,
                   static_cast<float>(barBounds.getX()), static_cast<float>(barBounds.getRight()));

    graphics.setColour(indicatorColour);
    graphics.fillEllipse(indicatorX - 10.0f, static_cast<float>(centreY) - 10.0f, 20.0f, 20.0f);
}

void TunerComponent::drawPitchMeter(juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    const auto palette = tunerPaletteFor(isDarkMode);
    const auto indicatorColour =
        std::abs(displayedCents) <= inTuneToleranceCents ? palette.inTune : palette.accent;

    graphics.setColour(palette.panel);
    graphics.fillRoundedRectangle(bounds.toFloat(), 8.0f);
    graphics.setColour(palette.outline);
    graphics.drawRoundedRectangle(bounds.toFloat(), 8.0f, 1.0f);

    const auto centre = juce::Point<float>(static_cast<float>(bounds.getCentreX()),
                                           static_cast<float>(bounds.getBottom() - 24));
    const auto radius = static_cast<float>(
        std::max(30, std::min(bounds.getWidth() / 2 - 28, bounds.getHeight() - 54)));

    constexpr double startAngle = -2.45;
    constexpr double endAngle = -0.69;

    juce::Path arc;
    for (int step = 0; step <= 64; ++step)
    {
        const auto proportion = static_cast<double>(step) / 64.0;
        const auto angle = startAngle + proportion * (endAngle - startAngle);
        const auto point =
            centre + juce::Point<float>(static_cast<float>(std::cos(angle) * radius),
                                        static_cast<float>(std::sin(angle) * radius));

        if (step == 0)
        {
            arc.startNewSubPath(point);
        }
        else
        {
            arc.lineTo(point);
        }
    }

    graphics.setColour(palette.outline);
    graphics.strokePath(arc, juce::PathStrokeType(5.0f));

    graphics.setFont(juce::FontOptions(12.0f));
    for (const auto cents : {-50, -25, 0, 25, 50})
    {
        const auto angle =
            juce::jmap(static_cast<double>(cents), -50.0, 50.0, startAngle, endAngle);
        const auto outerPoint =
            centre + juce::Point<float>(static_cast<float>(std::cos(angle) * radius),
                                        static_cast<float>(std::sin(angle) * radius));
        const auto innerPoint =
            centre + juce::Point<float>(static_cast<float>(std::cos(angle) * (radius - 15.0f)),
                                        static_cast<float>(std::sin(angle) * (radius - 15.0f)));

        graphics.setColour(cents == 0 ? palette.foreground : palette.muted);
        graphics.drawLine({innerPoint, outerPoint}, cents == 0 ? 2.0f : 1.0f);
    }

    const auto needleCents = hasSignal ? juce::jlimit(-50.0, 50.0, displayedCents) : 0.0;
    const auto needleAngle = juce::jmap(needleCents, -50.0, 50.0, startAngle, endAngle);
    const auto needleEnd =
        centre + juce::Point<float>(static_cast<float>(std::cos(needleAngle) * (radius - 20.0f)),
                                    static_cast<float>(std::sin(needleAngle) * (radius - 20.0f)));

    graphics.setColour(hasSignal ? indicatorColour : palette.muted);
    graphics.drawLine({centre, needleEnd}, 3.0f);
    graphics.fillEllipse(centre.x - 7.0f, centre.y - 7.0f, 14.0f, 14.0f);

    graphics.setColour(palette.muted);
    graphics.drawText("FLAT", bounds.removeFromLeft(74).removeFromBottom(24),
                      juce::Justification::centred);
    graphics.drawText("SHARP", bounds.removeFromRight(74).removeFromBottom(24),
                      juce::Justification::centred);
}

void TunerComponent::drawSelectedDisplay(juce::Graphics& graphics,
                                         juce::Rectangle<int> bounds) const
{
    switch (static_cast<DisplayMode>(displayModeBox.getSelectedId()))
    {
    case DisplayMode::bar:
        drawPitchBar(graphics, bounds);
        break;

    case DisplayMode::meter:
        drawPitchMeter(graphics, bounds);
        break;

    case DisplayMode::graph:
    default:
        drawPitchGraph(graphics, bounds);
        break;
    }
}

void TunerComponent::paint(juce::Graphics& graphics)
{
    const auto palette = tunerPaletteFor(isDarkMode);
    graphics.fillAll(palette.background);

    auto bounds = getLocalBounds().reduced(18);
    auto noteArea = bounds.removeFromTop(142);

    graphics.setColour(hasSignal ? palette.foreground : palette.muted);
    graphics.setFont(juce::FontOptions(78.0f, juce::Font::bold));
    graphics.drawText(displayedNote, noteArea.removeFromTop(96), juce::Justification::centred);

    graphics.setFont(juce::FontOptions(18.0f));
    graphics.drawFittedText(statusText(), noteArea.removeFromTop(42), juce::Justification::centred,
                            2);

    const auto preferredDisplayHeight = std::max(90, bounds.getHeight() - controlAreaHeight() - 8);
    displayBounds = bounds.removeFromTop(std::min(preferredDisplayHeight, bounds.getHeight()));

    drawSelectedDisplay(graphics, displayBounds);
}

void TunerComponent::resized()
{
    auto bounds = getLocalBounds().reduced(18);
    bounds.removeFromTop(142);

    const auto preferredDisplayHeight = std::max(90, bounds.getHeight() - controlAreaHeight() - 8);
    bounds.removeFromTop(std::min(preferredDisplayHeight, bounds.getHeight()));
    bounds.removeFromTop(8);

    auto displayRow = bounds.removeFromTop(32);
    displayModeLabel.setBounds(displayRow.removeFromLeft(110));
    displayModeBox.setBounds(displayRow);

    bounds.removeFromTop(8);
    advancedSettingsButton.setBounds(bounds.removeFromTop(34));

    if (!areAdvancedSettingsExpanded)
    {
        return;
    }

    bounds.removeFromTop(8);
    const auto placeSliderRow = [&bounds](juce::Label& label, juce::Slider& slider)
    {
        auto row = bounds.removeFromTop(30);
        label.setBounds(row.removeFromLeft(120));
        slider.setBounds(row);
    };

    placeSliderRow(easingLabel, easingSlider);
    placeSliderRow(averagingLabel, averagingSlider);
    placeSliderRow(thresholdLabel, thresholdSlider);
    placeSliderRow(dropoutLabel, dropoutSlider);
    placeSliderRow(durationLabel, durationSlider);

    bounds.removeFromTop(8);
    clearGraphButton.setBounds(bounds.removeFromTop(36).removeFromRight(120));
}
