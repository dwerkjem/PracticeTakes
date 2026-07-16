#include "SpectrogramComponent.h"

#include <algorithm>
#include <cmath>

SpectrogramComponent::SpectrogramComponent(
    juce::AudioDeviceManager& sharedAudioDeviceManager)
    : audioDeviceManager(sharedAudioDeviceManager)
{
    setOpaque(true);
    microphoneLabel.setColour(juce::Label::textColourId,
                              juce::Colour::fromRGB(142, 150, 166));
    microphoneLabel.setFont(juce::FontOptions(14.0f));
    addAndMakeVisible(microphoneLabel);

    spectrogramImage.clear(spectrogramImage.getBounds(),
                           juce::Colour::fromRGB(18, 20, 27));
    audioDeviceManager.addChangeListener(this);
    updateAudioDeviceStatus();
    startTimerHz(30);
}

SpectrogramComponent::~SpectrogramComponent()
{
    stopTimer();
    audioDeviceManager.removeChangeListener(this);
    detachAudioCallback();
}

void SpectrogramComponent::audioDeviceIOCallbackWithContext(
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

void SpectrogramComponent::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    currentSampleRate.store(device != nullptr ? device->getCurrentSampleRate()
                                              : 44100.0);
    audioFifo.reset();
}

void SpectrogramComponent::audioDeviceStopped()
{
    audioFifo.reset();
}

void SpectrogramComponent::changeListenerCallback(
    juce::ChangeBroadcaster* source)
{
    if (source == &audioDeviceManager)
        updateAudioDeviceStatus();
}

bool SpectrogramComponent::hasUsableInputDevice() const
{
    auto* device = audioDeviceManager.getCurrentAudioDevice();
    return device != nullptr && device->isOpen()
        && device->getActiveInputChannels().countNumberOfSetBits() > 0;
}

void SpectrogramComponent::attachAudioCallbackIfPossible()
{
    if (!audioCallbackAttached && hasUsableInputDevice())
    {
        audioDeviceManager.addAudioCallback(this);
        audioCallbackAttached = true;
    }
}

void SpectrogramComponent::detachAudioCallback()
{
    if (audioCallbackAttached)
    {
        audioDeviceManager.removeAudioCallback(this);
        audioCallbackAttached = false;
    }
}

void SpectrogramComponent::updateAudioDeviceStatus()
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
    }
    repaint();
}

void SpectrogramComponent::timerCallback()
{
    while (audioFifo.getNumReady() >= fftSize)
        calculateNextColumn();
    repaint(spectrogramBounds);
}

void SpectrogramComponent::calculateNextColumn()
{
    fftData.fill(0.0f);
    const auto scope = audioFifo.read(fftSize);
    std::copy_n(fifoBuffer.begin() + scope.startIndex1, scope.blockSize1,
                fftData.begin());
    std::copy_n(fifoBuffer.begin() + scope.startIndex2, scope.blockSize2,
                fftData.begin() + scope.blockSize1);

    window.multiplyWithWindowingTable(fftData.data(), fftSize);
    forwardFFT.performFrequencyOnlyForwardTransform(fftData.data());
    updateSpectrogramColumn();
}

void SpectrogramComponent::updateSpectrogramColumn()
{
    spectrogramImage.moveImageSection(0, 0, 1, 0,
                                      imageWidth - 1, imageHeight);

    for (int y = 0; y < imageHeight; ++y)
    {
        const auto proportion = 1.0 - static_cast<double>(y)
                                     / static_cast<double>(imageHeight - 1);
        const auto minFrequency = 40.0;
        const auto maxFrequency = std::min(20000.0,
                                           currentSampleRate.load() * 0.5);
        const auto frequency = minFrequency
            * std::pow(maxFrequency / minFrequency, proportion);
        const auto bin = juce::jlimit(0, fftSize / 2,
            static_cast<int>(frequency * fftSize / currentSampleRate.load()));
        const auto magnitude = fftData[static_cast<std::size_t>(bin)]
                               / static_cast<float>(fftSize);
        const auto decibels = juce::Decibels::gainToDecibels(magnitude, -100.0f);
        const auto level = juce::jmap(decibels, -90.0f, 0.0f, 0.0f, 1.0f);
        spectrogramImage.setPixelAt(imageWidth - 1, y,
                                    colourForLevel(level));
    }
}

juce::Colour SpectrogramComponent::colourForLevel(float level) const
{
    const auto clipped = juce::jlimit(0.0f, 1.0f, level);
    return juce::Colour::fromHSV(
        juce::jmap(clipped, 0.0f, 1.0f, 0.72f, 0.0f),
        juce::jmap(clipped, 0.0f, 1.0f, 0.45f, 1.0f),
        juce::jmap(clipped, 0.0f, 1.0f, 0.08f, 1.0f),
        1.0f);
}

float SpectrogramComponent::yForFrequency(double frequency) const
{
    const auto minimum = 40.0;
    const auto maximum = std::min(20000.0, currentSampleRate.load() * 0.5);
    const auto proportion = std::log(frequency / minimum)
                            / std::log(maximum / minimum);
    return juce::jmap(static_cast<float>(proportion), 0.0f, 1.0f,
                      static_cast<float>(spectrogramBounds.getBottom()),
                      static_cast<float>(spectrogramBounds.getY()));
}

void SpectrogramComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour::fromRGB(18, 20, 27));

    graphics.setColour(juce::Colour::fromRGB(25, 28, 37));
    graphics.fillRoundedRectangle(spectrogramBounds.toFloat(), 8.0f);

    if (audioErrorMessage.isEmpty())
    {
        graphics.drawImage(spectrogramImage,
                           spectrogramBounds.toFloat(),
                           juce::RectanglePlacement::stretchToFit);
    }
    else
    {
        graphics.setColour(juce::Colour::fromRGB(142, 150, 166));
        graphics.setFont(juce::FontOptions(17.0f));
        graphics.drawFittedText(audioErrorMessage,
                                spectrogramBounds.reduced(20),
                                juce::Justification::centred, 2);
    }

    graphics.setColour(juce::Colour::fromRGB(58, 65, 82));
    graphics.drawRoundedRectangle(spectrogramBounds.toFloat(), 8.0f, 1.0f);

    graphics.setColour(juce::Colour::fromRGB(188, 194, 207));
    graphics.setFont(juce::FontOptions(11.0f));
    for (const auto frequency : { 100.0, 500.0, 1000.0, 5000.0, 10000.0 })
    {
        if (frequency >= currentSampleRate.load() * 0.5)
            continue;
        const auto y = yForFrequency(frequency);
        graphics.drawHorizontalLine(static_cast<int>(y),
                                    static_cast<float>(spectrogramBounds.getX()),
                                    static_cast<float>(spectrogramBounds.getRight()));
        const auto label = frequency >= 1000.0
            ? juce::String(frequency / 1000.0, frequency == 1000.0 ? 0 : 1) + " kHz"
            : juce::String(static_cast<int>(frequency)) + " Hz";
        graphics.drawText(label,
                          spectrogramBounds.getX() + 6,
                          static_cast<int>(y) - 14,
                          60, 14,
                          juce::Justification::centredLeft);
    }
}

void SpectrogramComponent::resized()
{
    auto bounds = getLocalBounds().reduced(18);
    microphoneLabel.setBounds(bounds.removeFromTop(28));
    bounds.removeFromTop(8);
    spectrogramBounds = bounds;
}
