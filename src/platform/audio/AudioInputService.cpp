#include "AudioInputService.h"

#include "AudioRecoveryPolicy.h"

#include <algorithm>
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
static_assert(std::atomic<RealtimeRenderer*>::is_always_lock_free);

void storeMaximum(std::atomic<float>& destination, float value) noexcept
{
    auto observed = destination.load(std::memory_order_relaxed);
    while (observed < value &&
           !destination.compare_exchange_weak(observed, value, std::memory_order_relaxed))
    {
    }
}
} // namespace

AudioInputService::AudioInputService(bool declineInputDevice) : inputDeclined(declineInputDevice)
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

void AudioInputService::addStateListener(Listener* listener)
{
    if (listener == nullptr)
    {
        return;
    }

    {
        const juce::ScopedLock lock(consumerLock);
        if (std::find(stateListeners.begin(), stateListeners.end(), listener) !=
            stateListeners.end())
        {
            return;
        }
        stateListeners.push_back(listener);
    }

    // Told where things stand straight away, exactly as addListener tells a
    // sample listener, so a tool built mid-session does not wait for the next
    // change to know the microphone is muted.
    listener->audioInputStateChanged(inputState());
    if (deviceRunning.load(std::memory_order_acquire))
    {
        listener->audioInputAboutToStart(
            currentSampleRate.load(std::memory_order_acquire),
            currentInputChannels.load(std::memory_order_acquire));
    }
}

void AudioInputService::removeStateListener(Listener* listener)
{
    const juce::ScopedLock lock(consumerLock);
    stateListeners.erase(
        std::remove(stateListeners.begin(), stateListeners.end(), listener), stateListeners.end());
}

std::size_t AudioInputService::freeConsumerSlots() const
{
    const juce::ScopedLock lock(consumerLock);
    return static_cast<std::size_t>(std::count_if(
        consumers.begin(), consumers.end(),
        [](const ConsumerSlot& consumer) { return consumer.listener == nullptr; }));
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
    if (toneSourceRunning.load(std::memory_order_acquire) &&
        toneFrequency.load(std::memory_order_acquire) > 0.0f)
    {
        // Being fed a tone *is* having input. The generator running only to
        // keep the clock's time is not: it delivers no samples. A tool drawing "microphone
        // disconnected" over a graph of a signal it is being given would be
        // describing its own plumbing rather than what it has.
        return true;
    }

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

void AudioInputService::setRealtimeRenderer(RealtimeRenderer* newRenderer)
{
    const auto* previous = renderer.exchange(newRenderer, std::memory_order_acq_rel);

    // Whoever owns the previous renderer may destroy it once this returns, so
    // no callback may still be inside it. The callback never waits for this
    // thread; this thread waits for one bounded callback to finish. The
    // generator's blocks count as callbacks too, for the same reason.
    if (previous != nullptr && previous != newRenderer)
    {
        while (callbacksInProgress.load(std::memory_order_acquire) != 0)
        {
            juce::Thread::yield();
        }
    }

    // A renderer keeps time whether or not a device is open, so with none the
    // generator has to run to hand it blocks.
    updateToneSource();
}

RealtimeRenderer* AudioInputService::realtimeRenderer() const noexcept
{
    return renderer.load(std::memory_order_acquire);
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

    if (inputDeclined)
    {
        // Enumeration, and nothing else. `getAvailableDeviceTypes()` scans the
        // backends and picks a current device type without opening anything --
        // which is what keeps the Settings input-device chooser populated while
        // this instance holds no device. Checked against
        // AudioDeviceManager::scanDevicesIfNeeded() rather than assumed,
        // because the whole point of declining is that nothing opens a device.
        //
        // Here rather than in the constructor on purpose. Scanning creates the
        // backend objects and the timers they own, so doing it at construction
        // would make every test that merely builds a service enumerate ALSA --
        // the JUCE assertion wall `AudioInputServiceRecoveryAccess::attempt`
        // documents. This is the moment an ordinary instance would have opened
        // a device, which is the honest place to learn what devices exist.
        juce::ignoreUnused(manager.getAvailableDeviceTypes());
        publishState();

        return;
    }

    // Nobody asked for a device here -- they started the application, or they
    // imported settings. Opening one is automatic, so it is the same class of
    // work as the recovery timer and blocks the message thread the same way:
    // this is where a second instance stalled for six seconds before answering
    // anything, and where four of them stalled past any deadline worth having.
    //
    // A saved state has to be copied: the caller's XML does not outlive this
    // call, and the thread reading it starts after the call returns.
    auto saved = state != nullptr ? std::make_shared<juce::XmlElement>(*state)
                                  : std::shared_ptr<juce::XmlElement>{};

    startDeviceWork(
        [owner = ownedManager, saved]
        {
            owner->closeAudioDevice();
            juce::ignoreUnused(owner->initialise(2, 0, saved.get(), true));
        });
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

    // Stopped before `tone` is touched, not just before the frequency changes.
    // `reset()` below writes the generator's phase fields directly, and if the
    // source is already running -- changing the tone on an open state, which
    // the test-control channel does on every `open-state` -- the timer thread
    // is concurrently writing those same fields through
    // `renderToneBlock -> SyntheticTone::advance`. TSan caught the race:
    // `SyntheticTone.h:125` written from both threads with nothing ordering
    // them.
    //
    // `stopTimer` is the ordering. It blocks until any callback already in
    // flight has returned -- JUCE's HighResolutionTimer takes the same mutex
    // on both sides -- so nothing can still be inside `render()` once this
    // call returns, whether or not the timer happened to be running. Message
    // thread only; the audio thread never calls this.
    toneSource.stopTimer();
    toneSourceRunning.store(false, std::memory_order_release);

    // From a known point, so what a capture shows does not depend on how long
    // the capture before it took.
    tone.reset();
    toneAmplitude.store(juce::jlimit(0.0f, 1.0f, amplitude), std::memory_order_relaxed);
    toneFrequency.store(juce::jmax(0.0f, frequencyHz), std::memory_order_release);

    // A second block for the generator. Separate from the callback's, because
    // both can exist at once for the block it takes the generator to notice a
    // device has started, and they must not write into the same memory.
    if (generatedBlock.size() < maximumToneBlock)
    {
        generatedBlock.resize(maximumToneBlock);
    }

    updateToneSource();
    publishState();
}

void AudioInputService::updateToneSource()
{
    // Only with a tone asked for and nothing else delivering. A device is the
    // better signal when there is one -- it is what a person's microphone
    // sounds like -- and two producers is the thing `deliverSamples` guards
    // against rather than something to arrange on purpose.
    //
    // This needs no special case for a declined instance: `deviceRunning` is
    // never true in one, so the generator starts whenever a tone is asked for.
    // One rule -- "the device wins when there is one" -- rather than a second
    // that only happens to agree with it.
    //
    // A renderer wants blocks too, tone or no tone: the practice clock runs
    // from them, and with no device nothing else will hand it any.
    const auto wanted = (toneFrequency.load(std::memory_order_acquire) > 0.0f ||
                         renderer.load(std::memory_order_acquire) != nullptr) &&
                        !deviceRunning.load(std::memory_order_acquire);

    if (wanted == toneSourceRunning.load(std::memory_order_acquire))
    {
        return;
    }

    if (wanted)
    {
        // Told before it starts, the way a device tells them, so a tool has a
        // rate and a channel count before the first samples arrive.
        currentSampleRate.store(generatedSampleRate, std::memory_order_release);
        currentInputChannels.store(1, std::memory_order_release);
        toneSourceRunning.store(true, std::memory_order_release);
        formatVersion.fetch_add(1, std::memory_order_acq_rel);
        toneSource.startTimer(toneBlockMilliseconds);
    }
    else
    {
        // Stopped before the flag clears: `stopTimer` waits for a callback in
        // progress, so after this nothing is left that could still be filling.
        toneSource.stopTimer();
        toneSourceRunning.store(false, std::memory_order_release);
        formatVersion.fetch_add(1, std::memory_order_acq_rel);
    }
}

void AudioInputService::renderRealtime(
    const float* input,
    float* const* outputs,
    int outputChannels,
    int frames,
    double sampleRate) noexcept PRACTICE_TAKES_NONBLOCKING
{
    auto* active = renderer.load(std::memory_order_acquire);
    if (active == nullptr || frames <= 0)
    {
        return;
    }

    RenderBlock block;
    block.input = input;
    block.inputGain = gain.load(std::memory_order_relaxed);
    block.outputs = outputChannels > 0 ? outputs : nullptr;
    block.outputChannels = outputChannels > 0 ? outputChannels : 0;
    block.frames = frames;
    block.sampleRate = sampleRate;
    block.timeMilliseconds = juce::Time::getMillisecondCounterHiRes();
    active->render(block);
}

void AudioInputService::renderToneBlock() noexcept PRACTICE_TAKES_NONBLOCKING
{
    // Counted as a callback, so `setRealtimeRenderer` waits for this block as
    // it waits for a device's before letting a renderer go.
    AudioCallbackScope callbackScope(callbacksInProgress);

    const auto rate = currentSampleRate.load(std::memory_order_relaxed);
    const auto frames = static_cast<int>(std::min<double>(
        static_cast<double>(maximumToneBlock), rate * toneBlockMilliseconds / 1000.0));

    // The clock first, with no output: there is no device to hear it on, but
    // its position has to advance with the samples the tools are being fed.
    // Before the mute check, as in the device callback.
    renderRealtime(nullptr, nullptr, 0, frames, rate);

    const auto frequency = toneFrequency.load(std::memory_order_acquire);

    if (frequency <= 0.0f || generatedBlock.empty())
    {
        return;
    }

    // Mute means the tools receive nothing, whatever is producing the samples.
    // The audio callback returns before delivering for exactly this reason, and
    // this path has to agree with it: it bypasses the callback entirely, so
    // without this muting a device-free instance silenced nothing and a tool
    // went on drawing a reading from a microphone the user had just turned off.
    //
    // Before rendering rather than after, so the phase stops where the device
    // path's does. The generator keeps *running* -- `toneSourceRunning` stays
    // true, so `hasUsableInput()` still reports input and `inputState()` can
    // say `muted` rather than `disconnected`, which are not the same thing to a
    // person.
    if (muted.load(std::memory_order_relaxed))
    {
        return;
    }

    const auto count = std::min(generatedBlock.size(), static_cast<std::size_t>(frames));

    if (count == 0)
    {
        return;
    }

    tone.render(
        generatedBlock.data(), count, static_cast<double>(frequency), rate,
        toneAmplitude.load(std::memory_order_relaxed));
    deliverSamples(generatedBlock.data(), static_cast<int>(count));
}

void AudioInputService::ToneSource::hiResTimerCallback()
{
    service.renderToneBlock();
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

    // The clock, after the output is cleared and before the mute check: mute
    // silences the microphone, not the metronome.
    if (numSamples > 0 && renderer.load(std::memory_order_relaxed) != nullptr)
    {
        const float* deviceInput = nullptr;
        for (int channel = 0; channel < numInputChannels; ++channel)
        {
            if (inputChannelData[channel] != nullptr)
            {
                deviceInput = inputChannelData[channel];
                break;
            }
        }
        renderRealtime(
            deviceInput, outputChannelData, numOutputChannels, numSamples,
            currentSampleRate.load(std::memory_order_relaxed));
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

    deliverSamples(inputSamples, numSamples);
}

void AudioInputService::deliverSamples(const float* samples, int numSamples) noexcept
    PRACTICE_TAKES_NONBLOCKING
{
    // The one place samples enter the analysis path, whether they came from a
    // device or from the generator. Non-blocking because the device's caller is
    // the audio thread; the generator's caller simply inherits that.
    //
    // The token is what keeps the FIFOs single-producer. They are lock-free on
    // the assumption of exactly one writer, and a device starting while the
    // generator is mid-block would quietly be a second -- a corruption no test
    // would see, presenting as a tool drawing nonsense. An exchange, not a
    // lock: whoever loses skips this block, which is a dropped block and
    // already a thing the accounting knows how to say.
    if (filling.exchange(true, std::memory_order_acquire))
    {
        return;
    }

    const auto currentGain = gain.load(std::memory_order_relaxed);
    const auto sampleRange = juce::FloatVectorOperations::findMinAndMax(samples, numSamples);
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
            juce::ignoreUnused(
                consumer.fifo.push(samples, static_cast<std::size_t>(numSamples), currentGain));
        }
    }

    filling.store(false, std::memory_order_release);
}

void AudioInputService::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    // Stopped here, synchronously, before the device is announced as running
    // and before the first block can arrive. The audio callback renders the
    // tone itself whenever one is requested (see audioDeviceIOCallbackWithContext
    // -- it does that whether or not a device is present, so a device showing
    // up does not make it stop), which means the instant this device starts
    // delivering blocks, the audio thread becomes a second thing calling
    // tone.render() on the same SyntheticTone. Until now the ToneSource timer
    // thread was only told to stand down on the next updateToneSource() tick
    // -- up to one message-thread cycle later -- and both threads could be
    // inside render() together during that window, an unsynchronized write to
    // tone's phase fields. TSan caught the sibling of this bug (setSyntheticTone
    // racing the timer); this is the same race, entered a different way.
    //
    // Safe to block briefly here: every JUCE backend calls this synchronously
    // from AudioIODevice::start(), on the caller's thread, strictly before
    // registering the callback the real-time audio thread will invoke --
    // confirmed against ALSAAudioIODevice::start() rather than assumed. The
    // caller here is always a background thread (the recovery thread, the
    // explicit-init thread) or the message thread (choosing a device in
    // Settings), never the audio thread itself.
    if (toneSourceRunning.exchange(false, std::memory_order_acq_rel))
    {
        toneSource.stopTimer();
        formatVersion.fetch_add(1, std::memory_order_acq_rel);
    }

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

    // A device starting or going away changes whether the generator should be
    // running, and only this thread may start or stop a timer. Until the next
    // tick both could be delivering, which is what the token in
    // `deliverSamples` is for.
    updateToneSource();

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

std::vector<AudioInputService::Listener*> AudioInputService::listenerSnapshot() const
{
    std::vector<Listener*> snapshot;
    const juce::ScopedLock lock(consumerLock);
    snapshot.reserve(consumers.size() + stateListeners.size());
    for (const auto& consumer : consumers)
    {
        if (consumer.listener != nullptr)
        {
            snapshot.push_back(consumer.listener);
        }
    }
    snapshot.insert(snapshot.end(), stateListeners.begin(), stateListeners.end());
    return snapshot;
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
    // Quiet, not correctness -- `startDeviceWork` is what guarantees no device
    // is opened. Without this the timer would notice the absent device and
    // queue work that is then refused, once per tick, for the life of the
    // process.
    if (inputDeclined)
    {
        return;
    }

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
    startDeviceWork(reopen);
}

void AudioInputService::startDeviceWork(std::function<void()> step)
{
    // This is the guard that makes "a declined instance opens no device" hold.
    // Every open -- startup, an explicit user choice, a settings import, an
    // automatic recovery -- funnels through here, so one return covers all of
    // them. The matching return in `scanForDeviceChanges` is for quiet, not for
    // correctness: do not remove this one on the grounds that the scan already
    // stops, because the explicit-request paths do not go through the scan.
    if (inputDeclined)
    {
        return;
    }

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
        [step = std::move(step), inFlight]
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
