#include "SpectrogramComponent.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
constexpr std::array<double, 5> frequencyGridLines{100.0, 500.0, 1000.0, 5000.0, 10000.0};

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
SpectrogramComponent::SpectrogramComponent(juce::AudioDeviceManager& sharedAudioDeviceManager)
    : audioDeviceManager(sharedAudioDeviceManager)
{
    setOpaque(true);
    spectrogramImage.clear(spectrogramImage.getBounds(), backgroundColour());

    audioDeviceManager.addChangeListener(this);
    updateAudioDeviceStatus();
    startTimerHz(refreshRateHz);
}

SpectrogramComponent::~SpectrogramComponent()
{
    stopTimer();
    audioDeviceManager.removeChangeListener(this);
    detachAudioCallback();
}

//==============================================================================
// Appearance

void SpectrogramComponent::setDarkMode(bool shouldUseDarkMode)
{
    if (isDarkMode == shouldUseDarkMode)
    {
        return;
    }

    isDarkMode = shouldUseDarkMode;

    // Old columns use colours from the previous theme, so clear the image when
    // the appearance changes instead of mixing light and dark palettes.
    spectrogramImage.clear(spectrogramImage.getBounds(), backgroundColour());
    repaint();
}

juce::Colour SpectrogramComponent::backgroundColour() const
{
    return isDarkMode ? juce::Colour::fromRGB(18, 20, 27) : juce::Colour::fromRGB(235, 236, 238);
}

juce::Colour SpectrogramComponent::panelColour() const
{
    return isDarkMode ? juce::Colour::fromRGB(25, 28, 37) : juce::Colour::fromRGB(250, 250, 251);
}

juce::Colour SpectrogramComponent::mutedColour() const
{
    return isDarkMode ? juce::Colour::fromRGB(188, 194, 207) : juce::Colour::fromRGB(82, 88, 99);
}

juce::Colour SpectrogramComponent::outlineColour() const
{
    return isDarkMode ? juce::Colour::fromRGB(58, 65, 82) : juce::Colour::fromRGB(165, 169, 178);
}

//==============================================================================
// Audio capture

void SpectrogramComponent::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData, int numInputChannels, float* const* outputChannelData,
    int numOutputChannels, int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    // The tool is input-only. Clearing output avoids accidental feedback if an
    // output buffer is supplied by a future audio-device configuration.
    clearOutputChannels(outputChannelData, numOutputChannels, numSamples);

    if (numInputChannels <= 0 || inputChannelData[0] == nullptr)
    {
        return;
    }

    writeInputSamplesToFifo(inputChannelData[0], numSamples);
}

void SpectrogramComponent::writeInputSamplesToFifo(const float* inputSamples, int numSamples)
{
    const auto writableSamples = std::min(numSamples, audioFifo.getFreeSpace());
    const auto writeScope = audioFifo.write(writableSamples);

    std::copy_n(inputSamples, writeScope.blockSize1, fifoBuffer.begin() + writeScope.startIndex1);
    std::copy_n(inputSamples + writeScope.blockSize1, writeScope.blockSize2,
                fifoBuffer.begin() + writeScope.startIndex2);
}

void SpectrogramComponent::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    currentSampleRate.store(device != nullptr ? device->getCurrentSampleRate() : 44100.0);
    audioFifo.reset();
}

void SpectrogramComponent::audioDeviceStopped()
{
    audioFifo.reset();
}

void SpectrogramComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &audioDeviceManager)
    {
        updateAudioDeviceStatus();
    }
}

bool SpectrogramComponent::hasUsableInputDevice() const
{
    auto* device = audioDeviceManager.getCurrentAudioDevice();

    return device != nullptr && device->isOpen() &&
           device->getActiveInputChannels().countNumberOfSetBits() > 0;
}

void SpectrogramComponent::attachAudioCallbackIfPossible()
{
    if (isAudioCallbackAttached || !hasUsableInputDevice())
    {
        return;
    }

    audioDeviceManager.addAudioCallback(this);
    isAudioCallbackAttached = true;
}

void SpectrogramComponent::detachAudioCallback()
{
    if (!isAudioCallbackAttached)
    {
        return;
    }

    audioDeviceManager.removeAudioCallback(this);
    isAudioCallbackAttached = false;
}

void SpectrogramComponent::updateAudioDeviceStatus()
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
    }

    repaint();
}

//==============================================================================
// FFT and image generation

void SpectrogramComponent::timerCallback()
{
    // Process every complete FFT frame that has accumulated since the previous
    // timer tick. Partial frames remain in the FIFO for the next tick.
    while (audioFifo.getNumReady() >= fftSize)
    {
        calculateNextColumn();
    }

    repaint(spectrogramBounds);
}

void SpectrogramComponent::calculateNextColumn()
{
    fftData.fill(0.0f);
    const auto readScope = audioFifo.read(fftSize);

    std::copy_n(fifoBuffer.begin() + readScope.startIndex1, readScope.blockSize1, fftData.begin());
    std::copy_n(fifoBuffer.begin() + readScope.startIndex2, readScope.blockSize2,
                fftData.begin() + readScope.blockSize1);

    // The Hann window reduces spectral leakage before the FFT.
    hannWindow.multiplyWithWindowingTable(fftData.data(), fftSize);
    forwardFFT.performFrequencyOnlyForwardTransform(fftData.data());
    updateSpectrogramColumn();
}

void SpectrogramComponent::updateSpectrogramColumn()
{
    // Shift older data left by one pixel, leaving the rightmost column for the
    // newest FFT frame.
    spectrogramImage.moveImageSection(0, 0, 1, 0, imageWidth - 1, imageHeight);

    for (int imageRow = 0; imageRow < imageHeight; ++imageRow)
    {
        const auto frequency = frequencyForImageRow(imageRow);
        const auto fftBin = fftBinForFrequency(frequency);
        const auto magnitude =
            fftData[static_cast<std::size_t>(fftBin)] / static_cast<float>(fftSize);
        const auto decibels = juce::Decibels::gainToDecibels(magnitude, minimumAnalysisDecibels);
        const auto visibleLevel = juce::jmap(decibels, visibleDecibelFloor, 0.0f, 0.0f, 1.0f);

        spectrogramImage.setPixelAt(imageWidth - 1, imageRow, colourForLevel(visibleLevel));
    }
}

double SpectrogramComponent::maximumVisibleFrequency() const
{
    const auto nyquistFrequency = currentSampleRate.load() * 0.5;
    return std::min(maximumDisplayedFrequencyHz, nyquistFrequency);
}

double SpectrogramComponent::frequencyForImageRow(int imageRow) const
{
    // A logarithmic scale gives musical low and mid frequencies enough space
    // while still showing the upper spectrum.
    const auto verticalPosition =
        1.0 - static_cast<double>(imageRow) / static_cast<double>(imageHeight - 1);
    const auto maximumFrequency = maximumVisibleFrequency();

    return minimumDisplayedFrequencyHz *
           std::pow(maximumFrequency / minimumDisplayedFrequencyHz, verticalPosition);
}

int SpectrogramComponent::fftBinForFrequency(double frequency) const
{
    const auto sampleRate = currentSampleRate.load();
    if (sampleRate <= 0.0)
    {
        return 0;
    }

    const auto bin = static_cast<int>(frequency * fftSize / sampleRate);
    return juce::jlimit(0, fftSize / 2, bin);
}

juce::Colour SpectrogramComponent::colourForLevel(float level) const
{
    const auto clippedLevel = juce::jlimit(0.0f, 1.0f, level);

    if (isDarkMode)
    {
        return juce::Colour::fromHSV(juce::jmap(clippedLevel, 0.0f, 1.0f, 0.72f, 0.0f),
                                     juce::jmap(clippedLevel, 0.0f, 1.0f, 0.45f, 1.0f),
                                     juce::jmap(clippedLevel, 0.0f, 1.0f, 0.08f, 1.0f), 1.0f);
    }

    const auto quietColour = juce::Colour::fromRGB(246, 248, 252);
    const auto middleColour = juce::Colour::fromRGB(85, 139, 214);
    const auto loudColour = juce::Colour::fromRGB(208, 66, 53);
    constexpr float middlePoint = 0.62f;

    return clippedLevel < middlePoint
               ? quietColour.interpolatedWith(middleColour, clippedLevel / middlePoint)
               : middleColour.interpolatedWith(loudColour,
                                               (clippedLevel - middlePoint) / (1.0f - middlePoint));
}

//==============================================================================
// Drawing

float SpectrogramComponent::yForFrequency(double frequency) const
{
    const auto maximumFrequency = maximumVisibleFrequency();
    const auto logarithmicPosition = std::log(frequency / minimumDisplayedFrequencyHz) /
                                     std::log(maximumFrequency / minimumDisplayedFrequencyHz);

    return juce::jmap(static_cast<float>(logarithmicPosition), 0.0f, 1.0f,
                      static_cast<float>(spectrogramBounds.getBottom()),
                      static_cast<float>(spectrogramBounds.getY()));
}

juce::String SpectrogramComponent::frequencyLabel(double frequency) const
{
    if (frequency < 1000.0)
    {
        return juce::String(static_cast<int>(frequency)) + " Hz";
    }

    const auto decimalPlaces = frequency == 1000.0 ? 0 : 1;
    return juce::String(frequency / 1000.0, decimalPlaces) + " kHz";
}

void SpectrogramComponent::drawFrequencyGrid(juce::Graphics& graphics) const
{
    graphics.setColour(mutedColour());
    graphics.setFont(juce::FontOptions(11.0f));

    for (const auto frequency : frequencyGridLines)
    {
        if (frequency >= maximumVisibleFrequency())
        {
            continue;
        }

        const auto y = yForFrequency(frequency);
        graphics.drawHorizontalLine(static_cast<int>(y),
                                    static_cast<float>(spectrogramBounds.getX()),
                                    static_cast<float>(spectrogramBounds.getRight()));
        graphics.drawText(frequencyLabel(frequency), spectrogramBounds.getX() + 6,
                          static_cast<int>(y) - 14, 60, 14, juce::Justification::centredLeft);
    }
}

void SpectrogramComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(backgroundColour());

    graphics.setColour(panelColour());
    graphics.fillRoundedRectangle(spectrogramBounds.toFloat(), 8.0f);

    if (audioErrorMessage.isEmpty())
    {
        graphics.drawImage(spectrogramImage, spectrogramBounds.toFloat(),
                           juce::RectanglePlacement::stretchToFit);
    }
    else
    {
        graphics.setColour(mutedColour());
        graphics.setFont(juce::FontOptions(17.0f));
        graphics.drawFittedText(audioErrorMessage, spectrogramBounds.reduced(20),
                                juce::Justification::centred, 2);
    }

    graphics.setColour(outlineColour());
    graphics.drawRoundedRectangle(spectrogramBounds.toFloat(), 8.0f, 1.0f);

    drawFrequencyGrid(graphics);
}

void SpectrogramComponent::resized()
{
    spectrogramBounds = getLocalBounds().reduced(18);
}
