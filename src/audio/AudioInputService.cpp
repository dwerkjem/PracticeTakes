#include "AudioInputService.h"

#include "AudioRecoveryPolicy.h"

#include <cmath>

namespace
{
class AudioCallbackScope final
{
  public:
    explicit AudioCallbackScope(std::atomic<unsigned int>& counterToUse) noexcept
        : counter(counterToUse)
    {
        counter.fetch_add(1, std::memory_order_acq_rel);
    }

    ~AudioCallbackScope()
    {
        counter.fetch_sub(1, std::memory_order_acq_rel);
    }

  private:
    std::atomic<unsigned int>& counter;
};

static_assert(std::atomic<unsigned int>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<float>::is_always_lock_free);
static_assert(std::atomic<double>::is_always_lock_free);
static_assert(std::atomic<int>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

void storeMaximum(std::atomic<float>& destination, float value) noexcept
{
    auto observed = destination.load(std::memory_order_relaxed);
    while (observed < value &&
           !destination.compare_exchange_weak(observed, value, std::memory_order_relaxed))
    {
    }
}
} // namespace

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
    ticksUntilDeviceScan =
        hasUsableInput() ? connectedDeviceScanIntervalTicks : disconnectedDeviceScanIntervalTicks;
    startTimerHz(serviceRefreshRateHz);
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
    if (listener == nullptr)
        return;

    bool added = false;
    {
        const juce::ScopedLock lock(consumerLock);
        if (findConsumer(listener) != nullptr)
            return;

        for (auto& consumer : consumers)
        {
            if (consumer.listener == nullptr)
            {
                consumer.fifo.reset();
                consumer.listener = listener;
                consumer.active.store(true, std::memory_order_release);
                added = true;
                break;
            }
        }
    }

    jassert(added);
    if (!added)
        return;

    listener->audioInputStateChanged(inputState());
    if (deviceRunning.load(std::memory_order_acquire))
    {
        listener->audioInputAboutToStart(
            currentSampleRate.load(std::memory_order_acquire),
            currentInputChannels.load(std::memory_order_acquire));
    }
}

void AudioInputService::removeListener(Listener* listener)
{
    ConsumerSlot* removedConsumer = nullptr;
    {
        const juce::ScopedLock lock(consumerLock);
        removedConsumer = findConsumer(listener);
        if (removedConsumer == nullptr)
            return;
        removedConsumer->active.store(false, std::memory_order_release);
    }

    // The callback never waits for the message thread. Removal may wait for
    // the current bounded callback to finish before recycling this slot.
    while (callbacksInProgress.load(std::memory_order_acquire) != 0)
        juce::Thread::yield();

    const juce::ScopedLock lock(consumerLock);
    if (removedConsumer->listener == listener)
    {
        removedConsumer->fifo.reset();
        removedConsumer->listener = nullptr;
    }
}

std::size_t AudioInputService::availableSamples(Listener* listener) const
{
    const juce::ScopedLock lock(consumerLock);
    if (const auto* consumer = findConsumer(listener))
        return consumer->fifo.available();
    return 0;
}

std::size_t
AudioInputService::readSamples(Listener* listener, float* destination, std::size_t maximumSamples)
{
    const juce::ScopedLock lock(consumerLock);
    if (auto* consumer = findConsumer(listener))
        return consumer->fifo.pop(destination, maximumSamples);
    return 0;
}

void AudioInputService::discardPendingSamples(Listener* listener)
{
    const juce::ScopedLock lock(consumerLock);
    if (auto* consumer = findConsumer(listener))
        consumer->fifo.discardPending();
}

juce::AudioDeviceManager& AudioInputService::deviceManager() noexcept
{
    return manager;
}

bool AudioInputService::hasUsableInput() const
{
    auto* device = manager.getCurrentAudioDevice();
    // ALSA may report an empty active-channel bitset for a device that is open
    // and already delivering input callbacks. audioDeviceAboutToStart/stopped
    // is the authoritative lifecycle signal and avoids repeatedly reopening a
    // live device because of that backend reporting mismatch.
    return device != nullptr && device->isOpen() && deviceRunning.load(std::memory_order_acquire);
}

AudioInputService::InputState AudioInputService::inputState() const
{
    if (!hasUsableInput())
        return InputState::disconnected;
    if (muted.load(std::memory_order_relaxed))
        return InputState::muted;
    return clippingHoldTicks > 0 ? InputState::clipping : InputState::active;
}

bool AudioInputService::isMuted() const noexcept
{
    return muted.load(std::memory_order_relaxed);
}

void AudioInputService::setMuted(bool shouldBeMuted)
{
    if (muted.exchange(shouldBeMuted, std::memory_order_acq_rel) == shouldBeMuted)
        return;

    clippingDetected.store(false, std::memory_order_relaxed);
    clippingHoldTicks = 0;
    displayedInputLevel.store(0.0f, std::memory_order_relaxed);
    publishState();
    sendChangeMessage();
}

void AudioInputService::toggleMuted()
{
    setMuted(!isMuted());
}

float AudioInputService::inputGain() const noexcept
{
    return gain.load(std::memory_order_relaxed);
}

void AudioInputService::setInputGain(float newGain)
{
    const auto limitedGain = juce::jlimit(0.0f, 2.0f, newGain);
    if (std::abs(gain.exchange(limitedGain, std::memory_order_acq_rel) - limitedGain) < 0.0001f)
        return;
    sendChangeMessage();
}

float AudioInputService::inputLevel() const noexcept
{
    return juce::jlimit(0.0f, 1.0f, displayedInputLevel.load(std::memory_order_relaxed));
}

std::uint64_t AudioInputService::droppedAnalysisBlocks() const noexcept
{
    std::uint64_t total = 0;
    for (const auto& consumer : consumers)
        total += consumer.fifo.droppedBlocks();
    return total;
}

std::uint64_t AudioInputService::droppedAnalysisSamples() const noexcept
{
    std::uint64_t total = 0;
    for (const auto& consumer : consumers)
        total += consumer.fifo.droppedSamples();
    return total;
}

void AudioInputService::resetToDefaultInput()
{
    setInputGain(1.0f);
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
    // JUCE retains this explicit state even when the named input is absent and
    // opens the system default as a temporary fallback. createDeviceState()
    // will therefore keep the user's preference for a later reconnect/save.
    juce::ignoreUnused(manager.initialise(2, 0, &state, true));
    recovering = false;
    publishState();
}

std::unique_ptr<juce::XmlElement> AudioInputService::createDeviceState() const
{
    return manager.createStateXml();
}

void AudioInputService::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData,
    int numInputChannels,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext&)
{
    AudioCallbackScope callbackScope(callbacksInProgress);

    for (int channel = 0; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);

    if (numSamples <= 0 || muted.load(std::memory_order_relaxed))
        return;

    const float* inputSamples = nullptr;
    for (int channel = 0; channel < numInputChannels; ++channel)
    {
        if (inputChannelData[channel] != nullptr)
        {
            inputSamples = inputChannelData[channel];
            break;
        }
    }
    if (inputSamples == nullptr)
        return;

    const auto currentGain = gain.load(std::memory_order_relaxed);
    const auto sampleRange = juce::FloatVectorOperations::findMinAndMax(inputSamples, numSamples);
    const auto peakLevel =
        juce::jmax(std::abs(sampleRange.getStart()), std::abs(sampleRange.getEnd())) * currentGain;
    storeMaximum(peakSinceLastTimer, peakLevel);
    if (peakLevel >= 0.99f)
        clippingDetected.store(true, std::memory_order_relaxed);

    for (auto& consumer : consumers)
    {
        if (consumer.active.load(std::memory_order_acquire))
        {
            juce::ignoreUnused(consumer.fifo.push(
                inputSamples, static_cast<std::size_t>(numSamples), currentGain));
        }
    }
}

void AudioInputService::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    currentSampleRate.store(
        device != nullptr ? device->getCurrentSampleRate() : 44100.0, std::memory_order_release);
    currentInputChannels.store(
        device != nullptr ? device->getActiveInputChannels().countNumberOfSetBits() : 0,
        std::memory_order_release);
    deviceRunning.store(true, std::memory_order_release);
    formatVersion.fetch_add(1, std::memory_order_acq_rel);
}

void AudioInputService::audioDeviceStopped()
{
    deviceRunning.store(false, std::memory_order_release);
    formatVersion.fetch_add(1, std::memory_order_acq_rel);
}

void AudioInputService::changeListenerCallback(juce::ChangeBroadcaster*)
{
    if (hasUsableInput())
    {
        if (auto* device = manager.getCurrentAudioDevice())
            lastDeviceName = device->getName();
        recovering = false;
        ticksUntilDeviceScan = connectedDeviceScanIntervalTicks;
    }
    else
    {
        ticksUntilDeviceScan =
            juce::jmin(ticksUntilDeviceScan, disconnectedDeviceScanIntervalTicks);
    }
    publishState();
}

void AudioInputService::timerCallback()
{
    const auto previousLevel = displayedInputLevel.load(std::memory_order_relaxed);
    const auto capturedPeak =
        muted.load(std::memory_order_relaxed)
            ? 0.0f
            : peakSinceLastTimer.exchange(0.0f, std::memory_order_relaxed);
    const auto nextLevel = juce::jmax(capturedPeak, previousLevel * 0.72f);
    displayedInputLevel.store(nextLevel, std::memory_order_relaxed);

    if (clippingDetected.exchange(false, std::memory_order_relaxed))
        clippingHoldTicks = serviceRefreshRateHz * 3;
    else if (clippingHoldTicks > 0)
        --clippingHoldTicks;

    if (formatVersion.load(std::memory_order_acquire) != deliveredFormatVersion)
        deliverFormatChange();

    if (--ticksUntilDeviceScan <= 0)
    {
        scanForDeviceChanges();
        ticksUntilDeviceScan = hasUsableInput() ? connectedDeviceScanIntervalTicks
                                                : disconnectedDeviceScanIntervalTicks;
    }

    publishState();

    const auto droppedBlocks = droppedAnalysisBlocks();
    if (std::abs(previousLevel - nextLevel) >= 0.005f || droppedBlocks != lastReportedDroppedBlocks)
    {
        lastReportedDroppedBlocks = droppedBlocks;
        sendChangeMessage();
    }
}

AudioInputService::ConsumerSlot* AudioInputService::findConsumer(Listener* listener)
{
    for (auto& consumer : consumers)
        if (consumer.listener == listener)
            return &consumer;
    return nullptr;
}

const AudioInputService::ConsumerSlot* AudioInputService::findConsumer(Listener* listener) const
{
    for (const auto& consumer : consumers)
        if (consumer.listener == listener)
            return &consumer;
    return nullptr;
}

std::array<AudioInputService::Listener*, AudioInputService::maximumConsumers>
AudioInputService::listenerSnapshot() const
{
    std::array<Listener*, maximumConsumers> snapshot{};
    const juce::ScopedLock lock(consumerLock);
    for (std::size_t index = 0; index < consumers.size(); ++index)
        snapshot[index] = consumers[index].listener;
    // Copying nullable pointer values does not dereference them. The analyzer
    // can otherwise misdiagnose std::array's return copy as a null dereference.
    return snapshot; // NOLINT(clang-analyzer-core.NullDereference)
}

void AudioInputService::deliverFormatChange()
{
    deliveredFormatVersion = formatVersion.load(std::memory_order_acquire);
    const auto listeners = listenerSnapshot();
    const auto isRunning = deviceRunning.load(std::memory_order_acquire);
    const auto sampleRate = currentSampleRate.load(std::memory_order_acquire);
    const auto inputChannels = currentInputChannels.load(std::memory_order_acquire);

    for (auto* listener : listeners)
    {
        if (listener == nullptr)
            continue;
        discardPendingSamples(listener);
        if (isRunning)
            listener->audioInputAboutToStart(sampleRate, inputChannels);
        else
            listener->audioInputStopped();
    }
}

void AudioInputService::scanForDeviceChanges()
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

    auto* currentDevice = manager.getCurrentAudioDevice();
    if (AudioRecoveryPolicy::shouldAttemptRecovery(
            hasUsableInput(), currentDevice != nullptr,
            currentDevice != nullptr && currentDevice->isOpen(), hasEnumeratedInput) &&
        !recovering)
    {
        // Never tear down a device while its backend still considers it open.
        // In particular, closing a live ALSA mmap reader here can race its
        // capture thread. Wait for audioDeviceStopped/isOpen() to confirm that
        // the backend has finished before attempting recovery.
        recovering = true;

        juce::AudioDeviceManager::AudioDeviceSetup setup;
        manager.getAudioDeviceSetup(setup);

        if (manager.getCurrentAudioDevice() != nullptr)
            manager.closeAudioDevice();

        if (setup.inputDeviceName.isNotEmpty() || setup.outputDeviceName.isNotEmpty())
            manager.restartLastAudioDevice();
        else if (hasEnumeratedInput)
            juce::ignoreUnused(manager.initialise(2, 0, nullptr, true));

        recovering = false;
    }
}

void AudioInputService::publishState()
{
    const auto state = inputState();
    if (state == lastState)
        return;

    lastState = state;
    for (auto* listener : listenerSnapshot())
        if (listener != nullptr)
            listener->audioInputStateChanged(state);
    sendChangeMessage();
}
