#include "../AudioInputService.h"

AudioInputService::AudioInputService()
{
    manager.addChangeListener(this);
    manager.addAudioCallback(this);

    // With no saved preference yet, JUCE selects the current system default.
    const auto error = manager.initialise(2, 0, nullptr, true);
    juce::ignoreUnused(error);

    if (auto* device = manager.getCurrentAudioDevice())
        lastDeviceName = device->getName();

    lastAvailable = hasUsableInput();
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
    listener->audioInputStateChanged(hasUsableInput());
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

void AudioInputService::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                         int numInputChannels,
                                                         float* const* outputChannelData,
                                                         int numOutputChannels, int numSamples,
                                                         const juce::AudioIODeviceCallbackContext&)
{
    for (int channel = 0; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);

    if (numInputChannels > 0 && inputChannelData[0] != nullptr)
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
    const auto available = hasUsableInput();
    if (available == lastAvailable)
        return;

    lastAvailable = available;
    listeners.call(&Listener::audioInputStateChanged, available);
    sendChangeMessage();
}
