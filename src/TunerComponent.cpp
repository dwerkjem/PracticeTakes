#include "TunerComponent.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace
{
constexpr double minimumFrequency = 55.0;
constexpr double maximumFrequency = 1200.0;
constexpr float minimumRms = 0.008f;
constexpr double minimumCorrelation = 0.72;
const std::array<const char*, 12> noteNames {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};
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
        label.setColour(juce::Label::textColourId,
                        juce::Colour::fromRGB(188, 194, 207));
        label.setFont(juce::FontOptions(13.0f));
        addAndMakeVisible(label);
    };

    configureLabel(microphoneLabel, "Microphone: none selected");
    configureLabel(easingLabel, "Pitch easing");
    configureLabel(averagingLabel, "Average window");
    configureLabel(thresholdLabel, "Note switch");
    configureLabel(dropoutLabel, "Dropout hold");
    configureLabel(durationLabel, "Graph duration");

    configureSlider(easingSlider, 0.02, 1.0, 0.01, 0.35, "");
    configureSlider(averagingSlider, 1.0, 15.0, 1.0, 5.0, " samples");
    configureSlider(thresholdSlider, 0.1, 1.5, 0.05, 0.55, " st");
    configureSlider(dropoutSlider, 1.0, 20.0, 1.0, 4.0, " frames");
    configureSlider(durationSlider, 5.0, 60.0, 1.0, 20.0, " sec");

    clearGraphButton.onClick = [this]
    {
        graphHistory.clear();
        repaint(graphBounds);
    };
    addAndMakeVisible(clearGraphButton);

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

void TunerComponent::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData,
    int numInputChannels,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext&)
{
    for (int channel = 0; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);

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
    if (!audioCallbackAttached && hasUsableInputDevice())
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
    auto* device = audioDeviceManager.getCurrentAudioDevice();
    if (hasUsableInputDevice())
    {
        const auto setup = audioDeviceManager.getAudioDeviceSetup();
        microphoneLabel.setText(
            "Microphone: " + (setup.inputDeviceName.isNotEmpty()
                ? setup.inputDeviceName : device->getName()),
            juce::dontSendNotification);
        audioErrorMessage.clear();
        attachAudioCallbackIfPossible();
    }
    else
    {
        detachAudioCallback();
        microphoneLabel.setText("Microphone: none selected",
                                juce::dontSendNotification);
        audioErrorMessage = "Choose a microphone in the global Audio settings.";
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
        std::copy(samples.end() - analysisSize, samples.end(), analysisBuffer.begin());
    else
    {
        std::move(analysisBuffer.begin() + available,
                  analysisBuffer.end(), analysisBuffer.begin());
        std::copy(samples.begin(), samples.end(), analysisBuffer.end() - available);
    }
}

void TunerComponent::timerCallback()
{
    drainAudioFifo();
    const auto squareSum = std::inner_product(analysisBuffer.begin(),
        analysisBuffer.end(), analysisBuffer.begin(), 0.0);
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

    const auto minimumLag = std::max(2, static_cast<int>(sampleRate / maximumFrequency));
    const auto maximumLag = std::min(analysisSize / 2,
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
            const auto first = static_cast<double>(analysisBuffer[index]);
            const auto second = static_cast<double>(analysisBuffer[index + lag]);
            numerator += first * second;
            firstEnergy += first * first;
            secondEnergy += second * second;
        }
        const auto denominator = std::sqrt(firstEnergy * secondEnergy);
        const auto score = denominator > 0.0 ? numerator / denominator : 0.0;
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
    pitchAverageHistory[static_cast<std::size_t>(pitchHistoryWriteIndex)] = midiPitch;
    pitchHistoryWriteIndex = (pitchHistoryWriteIndex + 1) % maximumAverageWindow;
    pitchHistoryCount = std::min(pitchHistoryCount + 1, maximumAverageWindow);

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
        ? average : smoothedMidiNote + easing * (average - smoothedMidiNote);
    return 440.0 * std::pow(2.0, (smoothedMidiNote - 69.0) / 12.0);
}

void TunerComponent::updateNote(double frequency)
{
    const auto midi = 69.0 + 12.0 * std::log2(frequency / 440.0);
    const auto nearest = static_cast<int>(std::round(midi));
    if (!hasLockedMidiNote)
    {
        lockedMidiNote = nearest;
        hasLockedMidiNote = true;
    }
    else if (std::abs(midi - lockedMidiNote) > thresholdSlider.getValue())
        lockedMidiNote = nearest;

    const auto noteIndex = ((lockedMidiNote % 12) + 12) % 12;
    displayedNote = juce::String(noteNames[static_cast<std::size_t>(noteIndex)])
        + juce::String((lockedMidiNote / 12) - 1);
    displayedFrequency = frequency;
    displayedCents = 100.0 * (midi - lockedMidiNote);
}

void TunerComponent::addHistoryPoint(double midiPitch)
{
    graphHistory.push_back(midiPitch);
    const auto desired = juce::jlimit(100, maximumGraphPoints,
        static_cast<int>(durationSlider.getValue() * 20.0));
    if (static_cast<int>(graphHistory.size()) > desired)
        graphHistory.erase(graphHistory.begin(),
                           graphHistory.begin() + (graphHistory.size() - desired));
}

void TunerComponent::resetPitchTracking()
{
    pitchAverageHistory.fill(0.0);
    pitchHistoryCount = 0;
    pitchHistoryWriteIndex = 0;
    smoothedMidiNote = 0.0;
    hasLockedMidiNote = false;
    hasSignal = false;
    displayedFrequency = 0.0;
    displayedCents = 0.0;
    displayedNote = "--";
}

void TunerComponent::drawPitchGraph(juce::Graphics& graphics,
                                    juce::Rectangle<int> bounds) const
{
    graphics.setColour(juce::Colour::fromRGB(25, 28, 37));
    graphics.fillRoundedRectangle(bounds.toFloat(), 8.0f);
    graphics.setColour(juce::Colour::fromRGB(58, 65, 82));
    graphics.drawRoundedRectangle(bounds.toFloat(), 8.0f, 1.0f);

    if (graphHistory.size() < 2)
        return;

    std::vector<double> valid;
    for (const auto value : graphHistory)
        if (std::isfinite(value)) valid.push_back(value);
    if (valid.empty()) return;

    const auto minValue = *std::min_element(valid.begin(), valid.end()) - 0.5;
    const auto maxValue = *std::max_element(valid.begin(), valid.end()) + 0.5;
    juce::Path path;
    bool drawing = false;
    for (std::size_t index = 0; index < graphHistory.size(); ++index)
    {
        const auto value = graphHistory[index];
        if (!std::isfinite(value)) { drawing = false; continue; }
        const auto x = juce::jmap(static_cast<float>(index), 0.0f,
            static_cast<float>(graphHistory.size() - 1),
            static_cast<float>(bounds.getX()), static_cast<float>(bounds.getRight()));
        const auto y = juce::jmap(static_cast<float>(value),
            static_cast<float>(minValue), static_cast<float>(maxValue),
            static_cast<float>(bounds.getBottom()), static_cast<float>(bounds.getY()));
        if (drawing) path.lineTo(x, y); else { path.startNewSubPath(x, y); drawing = true; }
    }
    graphics.setColour(juce::Colour::fromRGB(100, 170, 255));
    graphics.strokePath(path, juce::PathStrokeType(2.0f));
}

void TunerComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour::fromRGB(18, 20, 27));
    auto bounds = getLocalBounds().reduced(18);
    auto top = bounds.removeFromTop(220);

    graphics.setColour(hasSignal ? juce::Colours::white
                                 : juce::Colour::fromRGB(142, 150, 166));
    graphics.setFont(juce::FontOptions(78.0f, juce::Font::bold));
    graphics.drawText(displayedNote, top.removeFromTop(100),
                      juce::Justification::centred);
    graphics.setFont(juce::FontOptions(18.0f));
    const auto status = audioErrorMessage.isNotEmpty() ? audioErrorMessage
        : hasSignal ? juce::String(displayedFrequency, 1) + " Hz   "
            + juce::String(displayedCents > 0.0 ? "+" : "")
            + juce::String(displayedCents, 1) + " cents"
        : "Play or sing a sustained note";
    graphics.drawFittedText(status, top.removeFromTop(42),
                            juce::Justification::centred, 2);

    graphBounds = bounds.removeFromTop(std::max(120, bounds.getHeight() / 2));
    drawPitchGraph(graphics, graphBounds);
}

void TunerComponent::resized()
{
    auto bounds = getLocalBounds().reduced(18);
    microphoneLabel.setBounds(bounds.removeFromTop(28));
    bounds.removeFromTop(192);
    bounds.removeFromTop(std::max(120, bounds.getHeight() / 2));
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
    clearGraphButton.setBounds(bounds.removeFromTop(34).removeFromRight(120));
}
