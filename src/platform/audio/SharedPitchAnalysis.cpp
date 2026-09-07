#include "SharedPitchAnalysis.h"

#include <algorithm>

SharedPitchAnalysis::SharedPitchAnalysis(AudioInputService& sharedAudioInputService)
    : audioInputService(sharedAudioInputService)
{
    audioInputService.addListener(this);
    startTimerHz(refreshRateHz);
}

SharedPitchAnalysis::~SharedPitchAnalysis()
{
    stopTimer();
    audioInputService.removeListener(this);
}

PitchDetector::Result SharedPitchAnalysis::latestResult() const noexcept
{
    return {
        latestFrequency.load(std::memory_order_relaxed),
        latestInputLevel.load(std::memory_order_relaxed)};
}

void SharedPitchAnalysis::audioInputAboutToStart(double sampleRate, int inputChannels)
{
    juce::ignoreUnused(inputChannels);
    currentSampleRate.store(sampleRate, std::memory_order_relaxed);
    audioInputService.discardPendingSamples(this);
    analysisBuffer.fill(0.0f);
}

void SharedPitchAnalysis::audioInputStopped()
{
    audioInputService.discardPendingSamples(this);
}

void SharedPitchAnalysis::audioInputStateChanged(AudioInputService::InputState)
{
    audioInputService.discardPendingSamples(this);
    latestFrequency.store(0.0, std::memory_order_relaxed);
    latestInputLevel.store(0.0f, std::memory_order_relaxed);
}

bool SharedPitchAnalysis::drainAudioFifo()
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

void SharedPitchAnalysis::timerCallback()
{
    if (!drainAudioFifo())
    {
        return;
    }

    const auto result = pitchDetector.detect(analysisBuffer, currentSampleRate.load());
    latestFrequency.store(result.frequency, std::memory_order_relaxed);
    latestInputLevel.store(result.inputLevel, std::memory_order_relaxed);
}
