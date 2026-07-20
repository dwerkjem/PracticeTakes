#include "AudioInputService.h"

AudioInputService::AudioInputService()
{
    manager.addChangeListener(this);
    manager.addAudioCallback(this);

    // With no saved preference yet, JUCE selects the current system default.
    const auto error = manager.initialise(2, 0, nullptr, true);
    juce::ignoreUnused(error);

    if (auto* device = manager.getCurrentAudioDevice())
        lastDeviceName = device->getName();

    lastState = inputState();
    startTimer(1500);
}

AudioInputService::~AudioInputService()
{
    stopTimer();
    manager.removeChangeListener(this);
    manager.removeAudioCallback(this);
    manager.closeAudioDevice();
}

void AudioInputService::addListener(Listener* listener)
{
    listeners.add(listener);
    listener->audioInputStateChanged(inputState());
    if (auto* device = manager.getCurrentAudioDevice(); device != nullptr && device->isOpen())
        listener->audioInputAboutToStart(device->getCurrentSampleRate());
}

void AudioInputService::removeListener(Listener* listener)
{
    listeners.remove(listener);
}

juce::AudioDeviceManager& AudioInputService::deviceManager() noexcept
{
    return manager;
}

bool AudioInputService::hasUsableInput() const
{
    auto* device = manager.getCurrentAudioDevice();
    return device != nullptr && device->isOpen() &&
           device->getActiveInputChannels().countNumberOfSetBits() > 0;
}

AudioInputService::InputState AudioInputService::inputState() const
{
    if (!hasUsableInput())
        return InputState::disconnected;
    if (muted.load())
        return InputState::muted;
    return clippingHoldTicks > 0 ? InputState::clipping : InputState::active;
}

bool AudioInputService::isMuted() const noexcept
{
    return muted.load();
}

void AudioInputService::setMuted(bool shouldBeMuted)
{
    if (muted.exchange(shouldBeMuted) == shouldBeMuted)
        return;

    clippingDetected.store(false);
    clippingHoldTicks = 0;
    publishState();
}

void AudioInputService::toggleMuted()
{
    setMuted(!isMuted());
}

void AudioInputService::resetToDefaultInput()
{
    recovering = true;
    manager.closeAudioDevice();
    juce::ignoreUnused(manager.initialise(2, 0, nullptr, true));
    recovering = false;
    publishState();
}

void AudioInputService::applySavedDeviceState(const juce::XmlElement& state)
{
    recovering = true;
    manager.closeAudioDevice();
    juce::ignoreUnused(manager.initialise(2, 0, &state, true));
    recovering = false;
    publishState();
}

std::unique_ptr<juce::XmlElement> AudioInputService::createDeviceState() const
{
    return manager.createStateXml();
}

void AudioInputService::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                         int numInputChannels,
                                                         float* const* outputChannelData,
                                                         int numOutputChannels, int numSamples,
                                                         const juce::AudioIODeviceCallbackContext&)
{
    for (int channel = 0; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);

    if (numInputChannels <= 0 || inputChannelData[0] == nullptr || muted.load())
        return;

    const auto sampleRange =
        juce::FloatVectorOperations::findMinAndMax(inputChannelData[0], numSamples);
    const auto peakLevel =
        juce::jmax(std::abs(sampleRange.getStart()), std::abs(sampleRange.getEnd()));
    if (peakLevel >= 0.99f)
        clippingDetected.store(true);

    listeners.call(&Listener::audioInputReceived, inputChannelData[0], numSamples);
}

void AudioInputService::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    const auto sampleRate = device != nullptr ? device->getCurrentSampleRate() : 44100.0;
    listeners.call(&Listener::audioInputAboutToStart, sampleRate);
}

void AudioInputService::audioDeviceStopped()
{
    listeners.call(&Listener::audioInputStopped);
}

void AudioInputService::changeListenerCallback(juce::ChangeBroadcaster*)
{
    if (hasUsableInput())
    {
        if (auto* device = manager.getCurrentAudioDevice())
            lastDeviceName = device->getName();
        recovering = false;
    }
    publishState();
}

void AudioInputService::timerCallback()
{
    if (clippingDetected.exchange(false))
        clippingHoldTicks = 2;
    else if (clippingHoldTicks > 0)
        --clippingHoldTicks;

    // Device backends refresh their lists when scanned. If the active device
    // vanished, restart the last setup; JUCE falls back to the system default
    // when that setup is no longer available. Repeated scans also recover a
    // reconnected device without restarting the application.
    bool hasEnumeratedInput = false;
    for (auto* type : manager.getAvailableDeviceTypes())
    {
        type->scanForDevices();
        hasEnumeratedInput = hasEnumeratedInput || !type->getDeviceNames(true).isEmpty();
    }

    if (!hasUsableInput() && !recovering)
    {
        recovering = true;

        juce::AudioDeviceManager::AudioDeviceSetup setup;
        manager.getAudioDeviceSetup(setup);

        // restartLastAudioDevice requires a previously opened setup. A machine
        // with no microphone (or one removed before startup) has empty device
        // names, so retry default initialisation only after an input appears.
        if (setup.inputDeviceName.isNotEmpty() || setup.outputDeviceName.isNotEmpty())
            manager.restartLastAudioDevice();
        else if (hasEnumeratedInput)
            juce::ignoreUnused(manager.initialise(2, 0, nullptr, true));

        recovering = false;
    }

    publishState();
}

void AudioInputService::publishState()
{
    const auto state = inputState();
    if (state == lastState)
        return;

    lastState = state;
    listeners.call(&Listener::audioInputStateChanged, state);
    sendChangeMessage();
}
