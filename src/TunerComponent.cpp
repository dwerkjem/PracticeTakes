#include "TunerComponent.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace
{
constexpr double minimumFrequency = 55.0;
constexpr double maximumFrequency = 1200.0;
constexpr float minimumRms = 0.008f;
constexpr double minimumCorrelation = 0.72;

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

TunerPalette tunerPaletteFor(bool darkMode)
{
    if (darkMode)
    {
        return {
            juce::Colour::fromRGB(18, 20, 27),
            juce::Colour::fromRGB(25, 28, 37),
            juce::Colour::fromRGB(54, 59, 72),
            juce::Colour::fromRGB(58, 65, 82),
            juce::Colour::fromRGB(238, 241, 247),
            juce::Colour::fromRGB(142, 150, 166),
            juce::Colour::fromRGB(100, 170, 255),
            juce::Colour::fromRGB(85, 214, 136)
        };
    }

    return {
        juce::Colour::fromRGB(235, 236, 238),
        juce::Colour::fromRGB(250, 250, 251),
        juce::Colour::fromRGB(220, 224, 230),
        juce::Colour::fromRGB(165, 169, 178),
        juce::Colour::fromRGB(28, 31, 37),
        juce::Colour::fromRGB(92, 98, 108),
        juce::Colour::fromRGB(55, 112, 196),
        juce::Colour::fromRGB(35, 145, 82)
    };
}

const std::array<const char*, 12> noteNames {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

juce::String noteNameForMidi(int midiNote)
{
    const auto noteIndex = ((midiNote % 12) + 12) % 12;
    const auto octave = (midiNote / 12) - 1;
    return juce::String(noteNames[static_cast<std::size_t>(noteIndex)])
        + juce::String(octave);
}
}

TunerComponent::TunerComponent(
    juce::AudioDeviceManager& sharedAudioDeviceManager)
    : audioDeviceManager(sharedAudioDeviceManager)
{
    setOpaque(true);

    const auto configureLabel = [this](juce::Label& label,
                                       const juce::String& text)
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
    displayModeBox.setSelectedId(static_cast<int>(DisplayMode::graph),
                                 juce::dontSendNotification);
    displayModeBox.onChange = [this]
    {
        updateGraphControlAvailability();
        resized();
        repaint();
    };
    addAndMakeVisible(displayModeBox);

    advancedSettingsButton.onClick = [this]
    {
        advancedSettingsExpanded = ! advancedSettingsExpanded;
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
    startTimerHz(20);
}

TunerComponent::~TunerComponent()
{
    stopTimer();
    audioDeviceManager.removeChangeListener(this);
    detachAudioCallback();
}

void TunerComponent::setDarkMode(bool shouldUseDarkMode)
{
    if (darkMode == shouldUseDarkMode)
        return;

    darkMode = shouldUseDarkMode;
    applyThemeToControls();
    repaint();
}

void TunerComponent::applyThemeToControls()
{
    const auto palette = tunerPaletteFor(darkMode);

    for (auto* label : { &displayModeLabel, &easingLabel, &averagingLabel,
                         &thresholdLabel, &dropoutLabel, &durationLabel })
    {
        label->setColour(juce::Label::textColourId, palette.muted);
    }

    for (auto* button : { &advancedSettingsButton, &clearGraphButton })
    {
        button->setColour(juce::TextButton::buttonColourId, palette.control);
        button->setColour(juce::TextButton::buttonOnColourId,
                          palette.accent.withAlpha(0.75f));
        button->setColour(juce::TextButton::textColourOffId,
                          palette.foreground);
        button->setColour(juce::TextButton::textColourOnId,
                          palette.foreground);
    }

    displayModeBox.setColour(juce::ComboBox::backgroundColourId, palette.control);
    displayModeBox.setColour(juce::ComboBox::textColourId, palette.foreground);
    displayModeBox.setColour(juce::ComboBox::outlineColourId, palette.outline);
    displayModeBox.setColour(juce::ComboBox::arrowColourId, palette.foreground);

    for (auto* slider : { &easingSlider, &averagingSlider, &thresholdSlider,
                           &dropoutSlider, &durationSlider })
    {
        slider->setColour(juce::Slider::backgroundColourId, palette.panel);
        slider->setColour(juce::Slider::trackColourId,
                          palette.accent.withAlpha(0.75f));
        slider->setColour(juce::Slider::thumbColourId, palette.accent);
        slider->setColour(juce::Slider::textBoxTextColourId, palette.foreground);
        slider->setColour(juce::Slider::textBoxBackgroundColourId,
                          palette.control);
        slider->setColour(juce::Slider::textBoxOutlineColourId,
                          palette.outline);
    }

    sendLookAndFeelChange();
}

void TunerComponent::configureSlider(juce::Slider& slider,
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
        advancedSettingsExpanded ? "Advanced settings  v"
                                 : "Advanced settings  >");

    for (auto* component : std::array<juce::Component*, 10> {
             &easingLabel, &easingSlider,
             &averagingLabel, &averagingSlider,
             &thresholdLabel, &thresholdSlider,
             &dropoutLabel, &dropoutSlider,
             &durationLabel, &durationSlider })
    {
        component->setVisible(advancedSettingsExpanded);
    }

    updateGraphControlAvailability();
}

void TunerComponent::updateGraphControlAvailability()
{
    const auto graphSelected =
        displayModeBox.getSelectedId() == static_cast<int>(DisplayMode::graph);

    durationLabel.setEnabled(graphSelected);
    durationSlider.setEnabled(graphSelected);
    clearGraphButton.setVisible(advancedSettingsExpanded && graphSelected);
}

void TunerComponent::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData,
    int numInputChannels,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext&)
{
    for (int channel = 0; channel < numOutputChannels; ++channel)
    {
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);
    }

    if (numInputChannels <= 0 || inputChannelData[0] == nullptr)
        return;

    const auto writable = std::min(numSamples, audioFifo.getFreeSpace());
    const auto scope = audioFifo.write(writable);

    std::copy_n(inputChannelData[0], scope.blockSize1,
                fifoBuffer.begin() + scope.startIndex1);
    std::copy_n(inputChannelData[0] + scope.blockSize1, scope.blockSize2,
                fifoBuffer.begin() + scope.startIndex2);
}

void TunerComponent::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    currentSampleRate.store(device != nullptr ? device->getCurrentSampleRate()
                                              : 44100.0);
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
        updateAudioDeviceStatus();
}

bool TunerComponent::hasUsableInputDevice() const
{
    auto* device = audioDeviceManager.getCurrentAudioDevice();
    return device != nullptr && device->isOpen()
        && device->getActiveInputChannels().countNumberOfSetBits() > 0;
}

void TunerComponent::attachAudioCallbackIfPossible()
{
    if (! audioCallbackAttached && hasUsableInputDevice())
    {
        audioDeviceManager.addAudioCallback(this);
        audioCallbackAttached = true;
    }
}

void TunerComponent::detachAudioCallback()
{
    if (audioCallbackAttached)
    {
        audioDeviceManager.removeAudioCallback(this);
        audioCallbackAttached = false;
    }
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
    const auto available = audioFifo.getNumReady();
    if (available <= 0)
        return;

    std::vector<float> samples(static_cast<std::size_t>(available));
    const auto scope = audioFifo.read(available);

    std::copy_n(fifoBuffer.begin() + scope.startIndex1, scope.blockSize1,
                samples.begin());
    std::copy_n(fifoBuffer.begin() + scope.startIndex2, scope.blockSize2,
                samples.begin() + scope.blockSize1);

    if (available >= analysisSize)
    {
        std::copy(samples.end() - analysisSize,
                  samples.end(), analysisBuffer.begin());
    }
    else
    {
        std::move(analysisBuffer.begin() + available,
                  analysisBuffer.end(), analysisBuffer.begin());
        std::copy(samples.begin(), samples.end(),
                  analysisBuffer.end() - available);
    }
}

void TunerComponent::timerCallback()
{
    drainAudioFifo();

    const auto squareSum = std::inner_product(
        analysisBuffer.begin(), analysisBuffer.end(),
        analysisBuffer.begin(), 0.0);
    inputLevel = static_cast<float>(
        std::sqrt(squareSum / static_cast<double>(analysisSize)));

    const auto frequency = detectPitch();
    if (frequency > 0.0)
    {
        framesWithoutPitch = 0;
        const auto stableFrequency = smoothFrequency(frequency);
        updateNote(stableFrequency);
        addHistoryPoint(smoothedMidiNote);
        hasSignal = true;
    }
    else
    {
        ++framesWithoutPitch;
        addHistoryPoint(std::numeric_limits<double>::quiet_NaN());
        if (framesWithoutPitch >= static_cast<int>(dropoutSlider.getValue()))
            resetPitchTracking();
    }

    repaint();
}

double TunerComponent::detectPitch() const
{
    const auto sampleRate = currentSampleRate.load();
    if (inputLevel < minimumRms || sampleRate <= 0.0)
        return 0.0;

    const auto minimumLag = std::max(
        2, static_cast<int>(sampleRate / maximumFrequency));
    const auto maximumLag = std::min(
        analysisSize / 2,
        static_cast<int>(sampleRate / minimumFrequency));

    int bestLag = 0;
    double bestCorrelation = 0.0;

    for (int lag = minimumLag; lag <= maximumLag; ++lag)
    {
        double numerator = 0.0;
        double firstEnergy = 0.0;
        double secondEnergy = 0.0;

        for (int index = 0; index < analysisSize - lag; ++index)
        {
            const auto first = static_cast<double>(
                analysisBuffer[static_cast<std::size_t>(index)]);
            const auto second = static_cast<double>(
                analysisBuffer[static_cast<std::size_t>(index + lag)]);
            numerator += first * second;
            firstEnergy += first * first;
            secondEnergy += second * second;
        }

        const auto denominator = std::sqrt(firstEnergy * secondEnergy);
        const auto score = denominator > 0.0
            ? numerator / denominator : 0.0;

        if (score > bestCorrelation)
        {
            bestCorrelation = score;
            bestLag = lag;
        }
    }

    return bestLag > 0 && bestCorrelation >= minimumCorrelation
        ? sampleRate / static_cast<double>(bestLag) : 0.0;
}

double TunerComponent::smoothFrequency(double frequency)
{
    const auto midiPitch = 69.0 + 12.0 * std::log2(frequency / 440.0);
    pitchAverageHistory[static_cast<std::size_t>(pitchHistoryWriteIndex)] =
        midiPitch;
    pitchHistoryWriteIndex =
        (pitchHistoryWriteIndex + 1) % maximumAverageWindow;
    pitchHistoryCount =
        std::min(pitchHistoryCount + 1, maximumAverageWindow);

    const auto requested = static_cast<int>(averagingSlider.getValue());
    const auto count = std::min(requested, pitchHistoryCount);
    double sum = 0.0;

    for (int offset = 0; offset < count; ++offset)
    {
        const auto index = (pitchHistoryWriteIndex - 1 - offset
                            + maximumAverageWindow) % maximumAverageWindow;
        sum += pitchAverageHistory[static_cast<std::size_t>(index)];
    }

    const auto average = sum / static_cast<double>(std::max(1, count));
    const auto easing = easingSlider.getValue();
    smoothedMidiNote = smoothedMidiNote == 0.0
        ? average
        : smoothedMidiNote + easing * (average - smoothedMidiNote);

    return 440.0 * std::pow(2.0, (smoothedMidiNote - 69.0) / 12.0);
}

void TunerComponent::updateNote(double frequency)
{
    const auto midi = 69.0 + 12.0 * std::log2(frequency / 440.0);
    const auto nearest = static_cast<int>(std::round(midi));

    if (! hasLockedMidiNote)
    {
        lockedMidiNote = nearest;
        hasLockedMidiNote = true;
    }
    else if (std::abs(midi - static_cast<double>(lockedMidiNote))
             > thresholdSlider.getValue())
    {
        lockedMidiNote = nearest;
    }

    displayedNote = noteNameForMidi(lockedMidiNote);
    displayedFrequency = frequency;
    displayedCents = 100.0 * (midi - static_cast<double>(lockedMidiNote));
}

void TunerComponent::addHistoryPoint(double midiPitch)
{
    graphHistory.push_back(midiPitch);
    const auto desired = juce::jlimit(
        100, maximumGraphPoints,
        static_cast<int>(durationSlider.getValue() * 20.0));

    if (static_cast<int>(graphHistory.size()) > desired)
    {
        graphHistory.erase(
            graphHistory.begin(),
            graphHistory.begin()
                + (static_cast<int>(graphHistory.size()) - desired));
    }
}

void TunerComponent::resetPitchTracking()
{
    pitchAverageHistory.fill(0.0);
    pitchHistoryCount = 0;
    pitchHistoryWriteIndex = 0;
    smoothedMidiNote = 0.0;
    framesWithoutPitch = 0;
    hasLockedMidiNote = false;
    hasSignal = false;
    displayedFrequency = 0.0;
    displayedCents = 0.0;
    displayedNote = "--";
}

void TunerComponent::drawPitchGraph(juce::Graphics& graphics,
                                    juce::Rectangle<int> bounds) const
{
    const auto palette = tunerPaletteFor(darkMode);

    graphics.setColour(palette.panel);
    graphics.fillRoundedRectangle(bounds.toFloat(), 8.0f);
    graphics.setColour(palette.outline);
    graphics.drawRoundedRectangle(bounds.toFloat(), 8.0f, 1.0f);

    auto content = bounds.reduced(8);
    auto labelArea = content.removeFromLeft(48);
    auto plotArea = content;

    std::vector<double> validValues;
    validValues.reserve(graphHistory.size());
    for (const auto value : graphHistory)
    {
        if (std::isfinite(value))
            validValues.push_back(value);
    }

    double minimumValue = hasLockedMidiNote
        ? static_cast<double>(lockedMidiNote) - 3.0 : 66.0;
    double maximumValue = hasLockedMidiNote
        ? static_cast<double>(lockedMidiNote) + 3.0 : 72.0;

    if (! validValues.empty())
    {
        minimumValue = *std::min_element(
            validValues.begin(), validValues.end()) - 0.5;
        maximumValue = *std::max_element(
            validValues.begin(), validValues.end()) + 0.5;

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
        static_cast<double>(plotArea.getHeight())
        / std::max(1.0, maximumValue - minimumValue);

    graphics.setFont(juce::FontOptions(11.0f));

    for (int midiNote = firstNote; midiNote <= lastNote; ++midiNote)
    {
        const auto y = juce::jmap(
            static_cast<float>(midiNote),
            static_cast<float>(minimumValue),
            static_cast<float>(maximumValue),
            static_cast<float>(plotArea.getBottom()),
            static_cast<float>(plotArea.getY()));
        const auto isCurrentNote = hasSignal && midiNote == lockedMidiNote;
        const auto isC = ((midiNote % 12) + 12) % 12 == 0;

        graphics.setColour(
            isCurrentNote ? palette.accent.withAlpha(0.48f)
                          : isC ? palette.muted.withAlpha(0.30f)
                                : palette.outline.withAlpha(0.62f));
        graphics.drawHorizontalLine(
            static_cast<int>(std::round(y)),
            static_cast<float>(plotArea.getX()),
            static_cast<float>(plotArea.getRight()));

        if (pixelsPerSemitone >= 10.0 || isCurrentNote || isC)
        {
            graphics.setColour(
                isCurrentNote ? palette.accent : palette.muted);
            graphics.drawFittedText(
                noteNameForMidi(midiNote),
                labelArea.withY(static_cast<int>(std::round(y)) - 8)
                         .withHeight(16),
                juce::Justification::centredRight,
                1);
        }
    }

    if (graphHistory.size() < 2 || validValues.empty())
        return;

    juce::Path path;
    bool drawing = false;

    for (std::size_t index = 0; index < graphHistory.size(); ++index)
    {
        const auto value = graphHistory[index];
        if (! std::isfinite(value))
        {
            drawing = false;
            continue;
        }

        const auto x = juce::jmap(
            static_cast<float>(index),
            0.0f,
            static_cast<float>(graphHistory.size() - 1),
            static_cast<float>(plotArea.getX()),
            static_cast<float>(plotArea.getRight()));
        const auto y = juce::jmap(
            static_cast<float>(value),
            static_cast<float>(minimumValue),
            static_cast<float>(maximumValue),
            static_cast<float>(plotArea.getBottom()),
            static_cast<float>(plotArea.getY()));

        if (drawing)
            path.lineTo(x, y);
        else
        {
            path.startNewSubPath(x, y);
            drawing = true;
        }
    }

    graphics.setColour(palette.accent);
    graphics.strokePath(path, juce::PathStrokeType(2.0f));
}

void TunerComponent::drawPitchBar(juce::Graphics& graphics,
                                  juce::Rectangle<int> bounds) const
{
    const auto palette = tunerPaletteFor(darkMode);
    const auto accent = std::abs(displayedCents) <= 5.0
        ? palette.inTune : palette.accent;

    graphics.setColour(palette.panel);
    graphics.fillRoundedRectangle(bounds.toFloat(), 8.0f);
    graphics.setColour(palette.outline);
    graphics.drawRoundedRectangle(bounds.toFloat(), 8.0f, 1.0f);

    auto bar = bounds.reduced(28, 34);
    const auto centreY = bar.getCentreY();

    graphics.setColour(palette.control);
    graphics.fillRoundedRectangle(
        bar.toFloat().withHeight(10.0f).withCentre(
            { static_cast<float>(bar.getCentreX()),
              static_cast<float>(centreY) }),
        5.0f);

    graphics.setFont(juce::FontOptions(12.0f));
    for (const auto cents : { -50, -25, 0, 25, 50 })
    {
        const auto x = juce::jmap(
            static_cast<float>(cents), -50.0f, 50.0f,
            static_cast<float>(bar.getX()),
            static_cast<float>(bar.getRight()));
        graphics.setColour(cents == 0 ? palette.foreground : palette.muted);
        graphics.drawVerticalLine(
            static_cast<int>(std::round(x)),
            static_cast<float>(centreY) - 18.0f,
            static_cast<float>(centreY) + 18.0f);
        graphics.drawText(
            juce::String(cents > 0 ? "+" : "") + juce::String(cents),
            static_cast<int>(x) - 24,
            centreY + 22,
            48,
            18,
            juce::Justification::centred);
    }

    if (hasSignal)
    {
        const auto x = juce::jmap(
            static_cast<float>(juce::jlimit(-50.0, 50.0, displayedCents)),
            -50.0f, 50.0f,
            static_cast<float>(bar.getX()),
            static_cast<float>(bar.getRight()));
        graphics.setColour(accent);
        graphics.fillEllipse(x - 10.0f,
                             static_cast<float>(centreY) - 10.0f,
                             20.0f, 20.0f);
    }
}

void TunerComponent::drawPitchMeter(juce::Graphics& graphics,
                                    juce::Rectangle<int> bounds) const
{
    const auto palette = tunerPaletteFor(darkMode);
    const auto accent = std::abs(displayedCents) <= 5.0
        ? palette.inTune : palette.accent;

    graphics.setColour(palette.panel);
    graphics.fillRoundedRectangle(bounds.toFloat(), 8.0f);
    graphics.setColour(palette.outline);
    graphics.drawRoundedRectangle(bounds.toFloat(), 8.0f, 1.0f);

    const auto centre = juce::Point<float>(
        static_cast<float>(bounds.getCentreX()),
        static_cast<float>(bounds.getBottom() - 24));
    const auto radius = static_cast<float>(std::max(
        30, std::min(bounds.getWidth() / 2 - 28, bounds.getHeight() - 54)));
    constexpr double startAngle = -2.45;
    constexpr double endAngle = -0.69;

    juce::Path arc;
    for (int step = 0; step <= 64; ++step)
    {
        const auto proportion = static_cast<double>(step) / 64.0;
        const auto angle = startAngle + proportion * (endAngle - startAngle);
        const auto point = centre + juce::Point<float>(
            static_cast<float>(std::cos(angle) * radius),
            static_cast<float>(std::sin(angle) * radius));
        if (step == 0)
            arc.startNewSubPath(point);
        else
            arc.lineTo(point);
    }

    graphics.setColour(palette.outline);
    graphics.strokePath(arc, juce::PathStrokeType(5.0f));

    graphics.setFont(juce::FontOptions(12.0f));
    for (const auto cents : { -50, -25, 0, 25, 50 })
    {
        const auto angle = juce::jmap(
            static_cast<double>(cents), -50.0, 50.0,
            startAngle, endAngle);
        const auto outer = centre + juce::Point<float>(
            static_cast<float>(std::cos(angle) * radius),
            static_cast<float>(std::sin(angle) * radius));
        const auto inner = centre + juce::Point<float>(
            static_cast<float>(std::cos(angle) * (radius - 15.0f)),
            static_cast<float>(std::sin(angle) * (radius - 15.0f)));
        graphics.setColour(cents == 0 ? palette.foreground : palette.muted);
        graphics.drawLine({ inner, outer }, cents == 0 ? 2.0f : 1.0f);
    }

    const auto needleCents = hasSignal
        ? juce::jlimit(-50.0, 50.0, displayedCents) : 0.0;
    const auto needleAngle = juce::jmap(
        needleCents, -50.0, 50.0, startAngle, endAngle);
    const auto needleEnd = centre + juce::Point<float>(
        static_cast<float>(std::cos(needleAngle) * (radius - 20.0f)),
        static_cast<float>(std::sin(needleAngle) * (radius - 20.0f)));

    graphics.setColour(hasSignal ? accent : palette.muted);
    graphics.drawLine({ centre, needleEnd }, 3.0f);
    graphics.fillEllipse(centre.x - 7.0f, centre.y - 7.0f, 14.0f, 14.0f);

    graphics.setColour(palette.muted);
    graphics.drawText("FLAT", bounds.removeFromLeft(74).removeFromBottom(24),
                      juce::Justification::centred);
    graphics.drawText("SHARP", bounds.removeFromRight(74).removeFromBottom(24),
                      juce::Justification::centred);
}

void TunerComponent::drawSelectedDisplay(
    juce::Graphics& graphics,
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
    const auto palette = tunerPaletteFor(darkMode);
    graphics.fillAll(palette.background);

    auto bounds = getLocalBounds().reduced(18);
    auto top = bounds.removeFromTop(142);

    graphics.setColour(hasSignal ? palette.foreground : palette.muted);
    graphics.setFont(juce::FontOptions(78.0f, juce::Font::bold));
    graphics.drawText(displayedNote, top.removeFromTop(96),
                      juce::Justification::centred);

    graphics.setFont(juce::FontOptions(18.0f));
    const auto status = audioErrorMessage.isNotEmpty()
        ? audioErrorMessage
        : hasSignal
            ? juce::String(displayedFrequency, 1) + " Hz   "
                + juce::String(displayedCents > 0.0 ? "+" : "")
                + juce::String(displayedCents, 1) + " cents"
            : juce::String("Play or sing a sustained note");
    graphics.drawFittedText(status, top.removeFromTop(42),
                            juce::Justification::centred, 2);

    const auto controlsHeight = 32 + 8 + 34
        + (advancedSettingsExpanded ? (5 * 30 + 8 + 36) : 0);
    const auto preferredDisplayHeight = std::max(
        90, bounds.getHeight() - controlsHeight - 8);
    displayBounds = bounds.removeFromTop(
        std::min(preferredDisplayHeight, bounds.getHeight()));

    drawSelectedDisplay(graphics, displayBounds);
}

void TunerComponent::resized()
{
    auto bounds = getLocalBounds().reduced(18);
    bounds.removeFromTop(142);

    const auto controlsHeight = 32 + 8 + 34
        + (advancedSettingsExpanded ? (5 * 30 + 8 + 36) : 0);
    const auto preferredDisplayHeight = std::max(
        90, bounds.getHeight() - controlsHeight - 8);
    bounds.removeFromTop(std::min(preferredDisplayHeight, bounds.getHeight()));
    bounds.removeFromTop(8);

    auto displayRow = bounds.removeFromTop(32);
    displayModeLabel.setBounds(displayRow.removeFromLeft(110));
    displayModeBox.setBounds(displayRow);

    bounds.removeFromTop(8);
    advancedSettingsButton.setBounds(bounds.removeFromTop(34));

    if (! advancedSettingsExpanded)
        return;

    bounds.removeFromTop(8);
    const auto placeRow = [&bounds](juce::Label& label, juce::Slider& slider)
    {
        auto row = bounds.removeFromTop(30);
        label.setBounds(row.removeFromLeft(120));
        slider.setBounds(row);
    };

    placeRow(easingLabel, easingSlider);
    placeRow(averagingLabel, averagingSlider);
    placeRow(thresholdLabel, thresholdSlider);
    placeRow(dropoutLabel, dropoutSlider);
    placeRow(durationLabel, durationSlider);

    bounds.removeFromTop(8);
    clearGraphButton.setBounds(
        bounds.removeFromTop(36).removeFromRight(120));
}
