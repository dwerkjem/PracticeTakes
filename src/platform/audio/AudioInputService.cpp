#include "AudioInputService.h"

#include "AudioRecoveryPolicy.h"

#include <cmath>
#include <thread>
#include <utility>

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
    startTimerHz(serviceRefreshRateHz);
}

AudioInputService::~AudioInputService()
{
    stopTimer();
    manager.removeChangeListener(this);

    // Before anything else, and unconditionally. This is what makes abandoning
    // a stuck recovery safe: the manager must not be able to call back into an
    // object that is going away. It is also safe to do *while* a recovery is
    // inside the device -- `removeAudioCallback` takes JUCE's callback lock,
    // and opening a device does not hold it. Checked against the JUCE source
    // rather than assumed, because the whole design rests on it.
    manager.removeAudioCallback(this);

    if (recoveryInFlight())
    {
        // Deliberately not closed, and deliberately not waited for. Closing
        // destroys the device object a recovery may be inside `open` on, and
        // waiting is the freeze this exists to fix, arriving at the moment
        // somebody is trying to escape it.
        //
        // The manager stays alive in the recovery thread's own copy and is
        // destroyed when that thread finally returns -- which closes the device
        // then. If it never returns, one manager and one thread are abandoned
        // for the life of the process, which is the price of being able to
        // quit at all.
        return;
    }

    manager.closeAudioDevice();
}

void AudioInputService::addListener(Listener* listener)
{
    if (listener == nullptr)
    {
        return;
    }

    bool added = false;
    {
        const juce::ScopedLock lock(consumerLock);
        if (findConsumer(listener) != nullptr)
        {
            return;
        }

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
    {
        return;
    }

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
        {
            return;
        }
        removedConsumer->active.store(false, std::memory_order_release);
    }

    // The callback never waits for the message thread. Removal may wait for
    // the current bounded callback to finish before recycling this slot.
    while (callbacksInProgress.load(std::memory_order_acquire) != 0)
    {
        juce::Thread::yield();
    }

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
    {
        return consumer->fifo.available();
    }
    return 0;
}

std::size_t
AudioInputService::readSamples(Listener* listener, float* destination, std::size_t maximumSamples)
{
    const juce::ScopedLock lock(consumerLock);
    if (auto* consumer = findConsumer(listener))
    {
        return consumer->fifo.pop(destination, maximumSamples);
    }
    return 0;
}

void AudioInputService::discardPendingSamples(Listener* listener)
{
    const juce::ScopedLock lock(consumerLock);
    if (auto* consumer = findConsumer(listener))
    {
        consumer->fifo.discardPending();
    }
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
    {
        // Having none and trying to get one look the same from here and are
        // not the same thing to a person: one means plug something in, the
        // other means something else is holding it.
        return recoveryInFlight() ? InputState::opening : InputState::disconnected;
    }
    if (muted.load(std::memory_order_relaxed))
    {
        return InputState::muted;
    }
    return clippingHoldTicks > 0 ? InputState::clipping : InputState::active;
}

bool AudioInputService::isMuted() const noexcept
{
    return muted.load(std::memory_order_relaxed);
}

void AudioInputService::setMuted(bool shouldBeMuted)
{
    if (muted.exchange(shouldBeMuted, std::memory_order_acq_rel) == shouldBeMuted)
    {
        return;
    }

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
    {
        return;
    }
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
    {
        total += consumer.fifo.droppedBlocks();
    }
    return total;
}

std::uint64_t AudioInputService::droppedAnalysisSamples() const noexcept
{
    std::uint64_t total = 0;
    for (const auto& consumer : consumers)
    {
        total += consumer.fifo.droppedSamples();
    }
    return total;
}

void AudioInputService::resetToDefaultInput()
{
    setInputGain(1.0f);
    initialiseInput(nullptr, true);
}

void AudioInputService::initialiseDefaultInput()
{
    initialiseInput(nullptr, false);
}

void AudioInputService::initialiseInput(const juce::XmlElement* state, bool force)
{
    if (initialised && !force)
    {
        return;
    }

    initialised = true;
    // Explicit, and on the message thread on purpose: somebody asked for this
    // and is waiting to see the result. The automatic retry is the one that
    // must not block, and it is the only one that moved.
    manager.closeAudioDevice();
    juce::ignoreUnused(manager.initialise(2, 0, state, true));
    publishState();
}

void AudioInputService::applySavedDeviceState(const juce::XmlElement& state)
{
    // JUCE retains this explicit state even when the named input is absent and
    // opens the system default as a temporary fallback. createDeviceState()
    // will therefore keep the user's preference for a later reconnect/save.
    initialiseInput(&state, false);
}

std::unique_ptr<juce::XmlElement> AudioInputService::createDeviceState() const
{
    return manager.createStateXml();
}

void AudioInputService::setSyntheticTone(float frequencyHz, float amplitude)
{
    // Allocated here, on the message thread, before the tone is switched on --
    // the callback only ever writes into a block that already exists.
    if (toneBlock.size() < maximumToneBlock)
    {
        toneBlock.resize(maximumToneBlock);
    }

    // From a known point, so what a capture shows does not depend on how long
    // the capture before it took.
    tone.reset();
    toneAmplitude.store(juce::jlimit(0.0f, 1.0f, amplitude), std::memory_order_relaxed);
    toneFrequency.store(juce::jmax(0.0f, frequencyHz), std::memory_order_release);
}

float AudioInputService::syntheticTone() const noexcept
{
    return toneFrequency.load(std::memory_order_acquire);
}

void AudioInputService::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData,
    int numInputChannels,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext&) noexcept PRACTICE_TAKES_NONBLOCKING
{
    AudioCallbackScope callbackScope(callbacksInProgress);

    for (int channel = 0; channel < numOutputChannels; ++channel)
    {
        if (outputChannelData[channel] != nullptr)
        {
            juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);
        }
    }

    if (numSamples <= 0 || muted.load(std::memory_order_relaxed))
    {
        return;
    }

    const float* inputSamples = nullptr;

    // A tone replaces the device's input rather than mixing with it, and is
    // read before the input pointers are even looked at -- so a machine with no
    // microphone still produces a surface with something to analyse, which is
    // the case verification cares about most.
    const auto frequency = toneFrequency.load(std::memory_order_acquire);

    if (frequency > 0.0f && static_cast<std::size_t>(numSamples) <= toneBlock.size())
    {
        tone.render(
            toneBlock.data(), static_cast<std::size_t>(numSamples), static_cast<double>(frequency),
            currentSampleRate.load(std::memory_order_relaxed),
            toneAmplitude.load(std::memory_order_relaxed));

        inputSamples = toneBlock.data();
    }
    else
    {
        for (int channel = 0; channel < numInputChannels; ++channel)
        {
            if (inputChannelData[channel] != nullptr)
            {
                inputSamples = inputChannelData[channel];
                break;
            }
        }
    }

    if (inputSamples == nullptr)
    {
        return;
    }

    const auto currentGain = gain.load(std::memory_order_relaxed);
    const auto sampleRange = juce::FloatVectorOperations::findMinAndMax(inputSamples, numSamples);
    const auto peakLevel =
        juce::jmax(std::abs(sampleRange.getStart()), std::abs(sampleRange.getEnd())) * currentGain;
    storeMaximum(peakSinceLastTimer, peakLevel);
    if (peakLevel >= 0.99f)
    {
        clippingDetected.store(true, std::memory_order_relaxed);
    }

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
        {
            lastDeviceName = device->getName();
        }
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
    {
        clippingHoldTicks = serviceRefreshRateHz * 3;
    }
    else if (clippingHoldTicks > 0)
    {
        --clippingHoldTicks;
    }

    if (formatVersion.load(std::memory_order_acquire) != deliveredFormatVersion)
    {
        deliverFormatChange();
    }

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
    {
        if (consumer.listener == listener)
        {
            return &consumer;
        }
    }
    return nullptr;
}

const AudioInputService::ConsumerSlot* AudioInputService::findConsumer(Listener* listener) const
{
    for (const auto& consumer : consumers)
    {
        if (consumer.listener == listener)
        {
            return &consumer;
        }
    }
    return nullptr;
}

std::array<AudioInputService::Listener*, AudioInputService::maximumConsumers>
AudioInputService::listenerSnapshot() const
{
    std::array<Listener*, maximumConsumers> snapshot{};
    const juce::ScopedLock lock(consumerLock);
    for (std::size_t index = 0; index < consumers.size(); ++index)
    {
        snapshot[index] = consumers[index].listener;
    }
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
        {
            continue;
        }
        discardPendingSamples(listener);
        if (isRunning)
        {
            listener->audioInputAboutToStart(sampleRate, inputChannels);
        }
        else
        {
            listener->audioInputStopped();
        }
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
        !recoveryInFlight())
    {
        // Never tear down a device while its backend still considers it open.
        // In particular, closing a live ALSA mmap reader here can race its
        // capture thread. Wait for audioDeviceStopped/isOpen() to confirm that
        // the backend has finished before attempting recovery.
        attemptRecovery();
    }
}

bool AudioInputService::recoveryInFlight() const
{
    return flight != nullptr && flight->running.load(std::memory_order_acquire);
}

void AudioInputService::attemptRecovery()
{
    if (recoveryInFlight())
    {
        return;
    }

    // A thread per attempt, detached, holding only what outlives this object: a
    // copy of the step, and the state it reports through. It is not joined
    // anywhere -- an open that never returns would make joining it a hang, and
    // the one place that would happen is shutdown, which is exactly when a hang
    // is least forgivable.
    //
    // At most one exists, because nothing starts a second while this one runs.
    // A recovery that never finishes therefore costs one abandoned thread and
    // one device manager, and no more however long the device stays busy.
    auto inFlight = std::make_shared<Flight>();
    flight = inFlight;

    std::thread(
        [step = reopen, inFlight]
        {
            step();
            inFlight->running.store(false, std::memory_order_release);
        })
        .detach();
}

void AudioInputService::reopenDevice(juce::AudioDeviceManager& deviceManager)
{
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup(setup);

    if (deviceManager.getCurrentAudioDevice() != nullptr)
    {
        deviceManager.closeAudioDevice();
    }

    if (setup.inputDeviceName.isNotEmpty() || setup.outputDeviceName.isNotEmpty())
    {
        deviceManager.restartLastAudioDevice();
    }
    else
    {
        // Nothing saved to go back to, so ask for whatever the system offers.
        // `scanForDeviceChanges` has already established that it offers
        // something; without that this would open the default output and call
        // it a recovery.
        juce::ignoreUnused(deviceManager.initialise(2, 0, nullptr, true));
    }
}

void AudioInputService::publishState()
{
    const auto state = inputState();
    if (state == lastState)
    {
        return;
    }

    lastState = state;
    for (auto* listener : listenerSnapshot())
    {
        if (listener != nullptr)
        {
            listener->audioInputStateChanged(state);
        }
    }
    sendChangeMessage();
}
