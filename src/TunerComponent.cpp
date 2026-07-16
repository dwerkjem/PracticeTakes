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
    "C", "C#", "D", "D#", "E", "F",
    "F#", "G", "G#", "A", "A#", "B"
};
}

TunerComponent::TunerComponent()
{
    setOpaque(true);
    setSize(640, 500);

    addAndMakeVisible(microphoneLabel);
    microphoneLabel.setColour(
        juce::Label::textColourId,
        juce::Colour::fromRGB(142, 150, 166));
    microphoneLabel.setFont(juce::FontOptions(15.0f));
    microphoneLabel.setJustificationType(juce::Justification::centredLeft);

    const auto configureButton = [](juce::TextButton& button)
    {
        button.setColour(
            juce::TextButton::buttonColourId,
            juce::Colour::fromRGB(54, 59, 72));
        button.setColour(
            juce::TextButton::buttonOnColourId,
            juce::Colour::fromRGB(70, 92, 130));
        button.setColour(
            juce::TextButton::textColourOffId,
            juce::Colour::fromRGB(238, 241, 247));
    };

    addAndMakeVisible(microphoneButton);
    configureButton(microphoneButton);
    microphoneButton.onClick = [this]
    {
        showAudioDeviceSelector();
    };

    configureButton(audioSettingsDoneButton);
    audioSettingsDoneButton.onClick = [this]
    {
        hideAudioDeviceSelector();
    };
    addChildComponent(audioSettingsDoneButton);

    // Configure the desired input width without opening a default device.
    // The empty DEVICESETUP state leaves audio closed until the user chooses
    // an input, while requesting two channels avoids fixed-stereo ALSA devices
    // being opened with an inconsistent callback layout.
    juce::XmlElement noDeviceState { "DEVICESETUP" };
    const auto initialisationError =
        audioDeviceManager.initialise(2, 0, &noDeviceState, false);

    audioErrorMessage = initialisationError.isNotEmpty()
        ? initialisationError
        : juce::String("Select a microphone to begin.");

    audioDeviceManager.addChangeListener(this);

    updateAudioDeviceStatus();
    startTimerHz(20);
}

TunerComponent::~TunerComponent()
{
    stopTimer();
    audioDeviceManager.removeChangeListener(this);
    detachAudioCallback();
    audioDeviceSelector.reset();
    audioDeviceManager.closeAudioDevice();
}

void TunerComponent::prepareToPlay(int, double sampleRate)
{
    currentSampleRate = sampleRate;
    audioFifo.reset();
    analysisBuffer.fill(0.0f);
}

void TunerComponent::getNextAudioBlock(
    const juce::AudioSourceChannelInfo& bufferToFill)
{
    if (bufferToFill.buffer == nullptr
        || bufferToFill.buffer->getNumChannels() == 0)
    {
        return;
    }

    const auto* input = bufferToFill.buffer->getReadPointer(
        0, bufferToFill.startSample);

    const auto writable = std::min(
        bufferToFill.numSamples, audioFifo.getFreeSpace());

    const auto writeScope = audioFifo.write(writable);

    std::copy_n(
        input,
        writeScope.blockSize1,
        fifoBuffer.begin() + writeScope.startIndex1);

    std::copy_n(
        input + writeScope.blockSize1,
        writeScope.blockSize2,
        fifoBuffer.begin() + writeScope.startIndex2);
}

void TunerComponent::releaseResources()
{
    audioFifo.reset();
}

void TunerComponent::timerCallback()
{
    drainAudioFifo();

    const auto squareSum = std::inner_product(
        analysisBuffer.begin(),
        analysisBuffer.end(),
        analysisBuffer.begin(),
        0.0);

    inputLevel = static_cast<float>(
        std::sqrt(squareSum / static_cast<double>(analysisSize)));

    const auto frequency = detectPitch();

    if (frequency > 0.0)
    {
        updateNote(frequency);
        hasSignal = true;
    }
    else
    {
        hasSignal = false;
        displayedFrequency = 0.0;
        displayedCents = 0.0;
        displayedNote = "--";
    }

    repaint();
}

void TunerComponent::changeListenerCallback(
    juce::ChangeBroadcaster* source)
{
    if (source == &audioDeviceManager)
    {
        updateAudioDeviceStatus();
    }
}

void TunerComponent::showAudioDeviceSelector()
{
    if (showingAudioDeviceSelector)
    {
        return;
    }

    if (audioDeviceSelector == nullptr)
    {
        audioDeviceSelector =
            std::make_unique<juce::AudioDeviceSelectorComponent>(
                audioDeviceManager,
                1,
                2,
                0,
                0,
                false,
                false,
                false,
                true);
        addChildComponent(*audioDeviceSelector);
    }

    showingAudioDeviceSelector = true;
    detachAudioCallback();

    microphoneLabel.setVisible(false);
    microphoneButton.setVisible(false);
    audioDeviceSelector->setVisible(true);
    audioSettingsDoneButton.setVisible(true);

    resized();
    repaint();
}

void TunerComponent::hideAudioDeviceSelector()
{
    if (! showingAudioDeviceSelector)
    {
        return;
    }

    showingAudioDeviceSelector = false;

    if (audioDeviceSelector != nullptr)
    {
        audioDeviceSelector->setVisible(false);
    }

    audioSettingsDoneButton.setVisible(false);
    microphoneLabel.setVisible(true);
    microphoneButton.setVisible(true);

    updateAudioDeviceStatus();
    resized();
    repaint();
}

bool TunerComponent::hasUsableInputDevice() const
{
    auto* currentDevice = audioDeviceManager.getCurrentAudioDevice();

    return currentDevice != nullptr
        && currentDevice->isOpen()
        && currentDevice->getActiveInputChannels().countNumberOfSetBits() > 0;
}

void TunerComponent::attachAudioCallbackIfPossible()
{
    if (audioCallbackAttached
        || showingAudioDeviceSelector
        || ! hasUsableInputDevice())
    {
        return;
    }

    audioSourcePlayer.setSource(this);
    audioDeviceManager.addAudioCallback(&audioSourcePlayer);
    audioCallbackAttached = true;
}

void TunerComponent::detachAudioCallback()
{
    if (! audioCallbackAttached)
    {
        return;
    }

    audioSourcePlayer.setSource(nullptr);
    audioDeviceManager.removeAudioCallback(&audioSourcePlayer);
    audioCallbackAttached = false;
}

void TunerComponent::updateAudioDeviceStatus()
{
    auto* currentDevice = audioDeviceManager.getCurrentAudioDevice();

    if (hasUsableInputDevice())
    {
        const auto setup = audioDeviceManager.getAudioDeviceSetup();
        const auto deviceName = setup.inputDeviceName.isNotEmpty()
            ? setup.inputDeviceName
            : currentDevice->getName();

        microphoneLabel.setText(
            "Microphone: " + deviceName,
            juce::dontSendNotification);
        audioErrorMessage.clear();
        attachAudioCallbackIfPossible();
    }
    else
    {
        detachAudioCallback();

        microphoneLabel.setText(
            "Microphone: none selected",
            juce::dontSendNotification);

        if (audioErrorMessage.isEmpty())
        {
            audioErrorMessage =
                "No usable microphone is selected. Open audio settings below.";
        }
    }

    repaint();
}

void TunerComponent::drainAudioFifo()
{
    const auto available = audioFifo.getNumReady();

    if (available <= 0)
    {
        return;
    }

    std::vector<float> newSamples(static_cast<std::size_t>(available));
    const auto readScope = audioFifo.read(available);

    std::copy_n(
        fifoBuffer.begin() + readScope.startIndex1,
        readScope.blockSize1,
        newSamples.begin());

    std::copy_n(
        fifoBuffer.begin() + readScope.startIndex2,
        readScope.blockSize2,
        newSamples.begin() + readScope.blockSize1);

    if (available >= analysisSize)
    {
        std::copy(
            newSamples.end() - analysisSize,
            newSamples.end(),
            analysisBuffer.begin());
        return;
    }

    std::move(
        analysisBuffer.begin() + available,
        analysisBuffer.end(),
        analysisBuffer.begin());

    std::copy(
        newSamples.begin(),
        newSamples.end(),
        analysisBuffer.end() - available);
}

double TunerComponent::detectPitch() const
{
    if (inputLevel < minimumRms || currentSampleRate <= 0.0)
    {
        return 0.0;
    }

    const auto minimumLag = std::max(
        2, static_cast<int>(currentSampleRate / maximumFrequency));

    const auto maximumLag = std::min(
        analysisSize / 2,
        static_cast<int>(currentSampleRate / minimumFrequency));

    auto bestLag = 0;
    auto bestCorrelation = 0.0;

    for (auto lag = minimumLag; lag <= maximumLag; ++lag)
    {
        auto numerator = 0.0;
        auto firstEnergy = 0.0;
        auto secondEnergy = 0.0;
        const auto sampleCount = analysisSize - lag;

        for (auto index = 0; index < sampleCount; ++index)
        {
            const auto first = static_cast<double>(analysisBuffer[static_cast<std::size_t>(index)]);
            const auto second =
                static_cast<double>(analysisBuffer[static_cast<std::size_t>(index + lag)]);

            numerator += first * second;
            firstEnergy += first * first;
            secondEnergy += second * second;
        }

        const auto denominator = std::sqrt(firstEnergy * secondEnergy);
        const auto score = denominator > 0.0
            ? numerator / denominator
            : 0.0;

        if (score > bestCorrelation)
        {
            bestCorrelation = score;
            bestLag = lag;
        }
    }

    if (bestLag == 0 || bestCorrelation < minimumCorrelation)
    {
        return 0.0;
    }

    auto refinedLag = static_cast<double>(bestLag);

    if (bestLag > minimumLag && bestLag < maximumLag)
    {
        const auto scoreAt = [this](int lag)
        {
            auto numerator = 0.0;
            auto firstEnergy = 0.0;
            auto secondEnergy = 0.0;

            for (auto index = 0; index < analysisSize - lag; ++index)
            {
                const auto first =
                    static_cast<double>(analysisBuffer[static_cast<std::size_t>(index)]);
                const auto second =
                    static_cast<double>(analysisBuffer[static_cast<std::size_t>(index + lag)]);

                numerator += first * second;
                firstEnergy += first * first;
                secondEnergy += second * second;
            }

            const auto denominator = std::sqrt(firstEnergy * secondEnergy);
            return denominator > 0.0 ? numerator / denominator : 0.0;
        };

        const auto left = scoreAt(bestLag - 1);
        const auto center = scoreAt(bestLag);
        const auto right = scoreAt(bestLag + 1);
        const auto curvature = left - (2.0 * center) + right;

        if (std::abs(curvature) > 1.0e-12)
        {
            refinedLag += 0.5 * (left - right) / curvature;
        }
    }

    return currentSampleRate / refinedLag;
}

void TunerComponent::updateNote(double frequency)
{
    const auto midiNote =
        69.0 + (12.0 * std::log2(frequency / 440.0));
    const auto nearestMidi = static_cast<int>(std::round(midiNote));
    const auto noteIndex = ((nearestMidi % 12) + 12) % 12;
    const auto octave = (nearestMidi / 12) - 1;

    displayedFrequency = displayedFrequency <= 0.0
        ? frequency
        : (0.75 * displayedFrequency) + (0.25 * frequency);

    displayedCents = 100.0 * (midiNote - nearestMidi);
    displayedNote =
        juce::String(noteNames[static_cast<std::size_t>(noteIndex)])
        + juce::String(octave);
}

void TunerComponent::paint(juce::Graphics& graphics)
{
    const auto background = juce::Colour::fromRGB(18, 20, 27);
    const auto foreground = juce::Colour::fromRGB(238, 241, 247);
    const auto muted = juce::Colour::fromRGB(142, 150, 166);
    const auto accent = std::abs(displayedCents) <= 5.0
        ? juce::Colour::fromRGB(85, 214, 136)
        : juce::Colour::fromRGB(100, 170, 255);

    graphics.fillAll(background);

    if (showingAudioDeviceSelector)
    {
        graphics.setColour(foreground);
        graphics.setFont(juce::FontOptions(22.0f, juce::Font::bold));
        graphics.drawText(
            "AUDIO INPUT SETTINGS",
            getLocalBounds().reduced(24).removeFromTop(40),
            juce::Justification::centred);
        return;
    }

    auto bounds = getLocalBounds().reduced(32);
    bounds.removeFromBottom(56);

    graphics.setColour(muted);
    graphics.setFont(juce::FontOptions(18.0f));
    graphics.drawText(
        "PRACTICE TAKES  /  TUNER",
        bounds.removeFromTop(32),
        juce::Justification::centred);

    graphics.setColour(hasSignal ? foreground : muted);
    graphics.setFont(juce::FontOptions(104.0f, juce::Font::bold));
    graphics.drawText(
        displayedNote,
        bounds.removeFromTop(145),
        juce::Justification::centred);

    graphics.setFont(juce::FontOptions(22.0f));
    const auto frequencyText = audioErrorMessage.isNotEmpty()
        ? juce::String("Microphone unavailable")
        : hasSignal
            ? juce::String(displayedFrequency, 1) + " Hz"
            : juce::String("Play or sing a sustained note");

    graphics.drawText(
        frequencyText,
        bounds.removeFromTop(38),
        juce::Justification::centred);

    auto meter = bounds.removeFromTop(100).reduced(20, 24);
    const auto centerX = meter.getCentreX();

    graphics.setColour(juce::Colour::fromRGB(54, 59, 72));
    graphics.fillRoundedRectangle(
        meter.toFloat().withHeight(8.0f).withY(
            static_cast<float>(meter.getCentreY()) - 4.0f),
        4.0f);

    for (const auto cents : { -50, -25, 0, 25, 50 })
    {
        const auto x = juce::jmap(
            static_cast<float>(cents),
            -50.0f,
            50.0f,
            static_cast<float>(meter.getX()),
            static_cast<float>(meter.getRight()));

        graphics.setColour(cents == 0 ? foreground : muted);
        graphics.drawVerticalLine(
            static_cast<int>(x),
            static_cast<float>(meter.getCentreY()) - 14.0f,
            static_cast<float>(meter.getCentreY()) + 14.0f);
    }

    if (hasSignal)
    {
        const auto needleX = juce::jmap(
            static_cast<float>(juce::jlimit(
                -50.0, 50.0, displayedCents)),
            -50.0f,
            50.0f,
            static_cast<float>(meter.getX()),
            static_cast<float>(meter.getRight()));

        graphics.setColour(accent);
        graphics.fillEllipse(
            needleX - 9.0f,
            static_cast<float>(meter.getCentreY()) - 9.0f,
            18.0f,
            18.0f);
    }

    graphics.setColour(hasSignal ? accent : muted);
    graphics.setFont(juce::FontOptions(18.0f));

    const auto centsText = audioErrorMessage.isNotEmpty()
        ? audioErrorMessage
        : hasSignal
            ? juce::String(displayedCents > 0.0 ? "+" : "")
                + juce::String(displayedCents, 1) + " cents"
            : juce::String("Waiting for microphone input");

    graphics.drawFittedText(
        centsText,
        bounds.removeFromTop(46),
        juce::Justification::centred,
        2);

    const auto levelWidth = juce::jlimit(
        0.0f,
        static_cast<float>(bounds.getWidth()),
        inputLevel * 900.0f);

    auto levelBounds = bounds.removeFromTop(16).reduced(40, 5);
    graphics.setColour(juce::Colour::fromRGB(54, 59, 72));
    graphics.fillRoundedRectangle(levelBounds.toFloat(), 3.0f);
    graphics.setColour(accent.withAlpha(0.75f));
    graphics.fillRoundedRectangle(
        levelBounds.toFloat().withWidth(
            std::min(levelWidth, static_cast<float>(levelBounds.getWidth()))),
        3.0f);

    juce::ignoreUnused(centerX);
}

void TunerComponent::resized()
{
    if (showingAudioDeviceSelector)
    {
        auto settingsBounds = getLocalBounds().reduced(24);
        settingsBounds.removeFromTop(46);
        auto footer = settingsBounds.removeFromBottom(44);

        if (audioDeviceSelector != nullptr)
        {
            audioDeviceSelector->setBounds(settingsBounds);
        }

        audioSettingsDoneButton.setBounds(
            footer.removeFromRight(120).reduced(0, 4));
        return;
    }

    auto controls = getLocalBounds().reduced(32).removeFromBottom(44);
    auto buttonBounds = controls.removeFromRight(190);
    controls.removeFromRight(12);

    microphoneLabel.setBounds(controls);
    microphoneButton.setBounds(buttonBounds);
}
