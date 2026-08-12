#pragma once

// Modular JUCE includes rather than the generated JuceHeader.h umbrella:
// JuceHeader.h is produced for the PracticeTakes application target only, so a
// file that uses it cannot be compiled into PracticeTakesTests. That is the
// mechanical reason this service had no tests.
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "AudioSampleFifo.h"
#include "RealtimeSafety.h"
#include "SyntheticTone.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// Owns the application's only microphone callback and fans captured input into
// one bounded SPSC FIFO per analysis tool. Tools pull from their own FIFO on
// the message thread; no tool code runs in the device callback.
class AudioInputService final
    : public juce::ChangeBroadcaster,
      private juce::AudioIODeviceCallback,
      private juce::ChangeListener,
      private juce::Timer
{
  public:
    enum class InputState
    {
        disconnected,
        // Trying to open one. Distinct from having none, because "no
        // microphone" and "the microphone is not answering" have different
        // answers, and the second is the one somebody would otherwise read as
        // the application having crashed.
        opening,
        muted,
        active,
        clipping
    };

    class Listener
    {
      public:
        virtual ~Listener() = default;
        virtual void audioInputAboutToStart(double sampleRate, int inputChannels) = 0;
        virtual void audioInputStopped() = 0;
        virtual void audioInputStateChanged(InputState state) = 0;
    };

    AudioInputService();
    ~AudioInputService() override;

    void addListener(Listener* listener);
    void removeListener(Listener* listener);

    [[nodiscard]] std::size_t availableSamples(Listener* listener) const;
    [[nodiscard]] std::size_t
    readSamples(Listener* listener, float* destination, std::size_t maximumSamples);
    void discardPendingSamples(Listener* listener);

    [[nodiscard]] juce::AudioDeviceManager& deviceManager() noexcept;
    [[nodiscard]] bool hasUsableInput() const;
    [[nodiscard]] InputState inputState() const;
    [[nodiscard]] bool isMuted() const noexcept;
    void setMuted(bool shouldBeMuted);
    void toggleMuted();

    // Feed a synthetic tone instead of the device's input, so a captured
    // surface shows a tool analysing something without anybody making a noise
    // at a microphone. Zero hertz returns to the real input.
    void setSyntheticTone(float frequencyHz, float amplitude = 0.45f);
    [[nodiscard]] float syntheticTone() const noexcept;

    [[nodiscard]] float inputGain() const noexcept;
    void setInputGain(float gain);
    [[nodiscard]] float inputLevel() const noexcept;
    [[nodiscard]] std::uint64_t droppedAnalysisBlocks() const noexcept;
    [[nodiscard]] std::uint64_t droppedAnalysisSamples() const noexcept;

    void initialiseDefaultInput();
    void resetToDefaultInput();
    void applySavedDeviceState(const juce::XmlElement& state);
    [[nodiscard]] std::unique_ptr<juce::XmlElement> createDeviceState() const;

  private:
    // The device callback is private, and the AudioIODeviceCallback base is
    // inherited privately, because nothing in the application may invoke it --
    // only the device does. A test must, though, and for a reason that is not
    // convenience: the real-time contract in
    // docs/development/performance/audio-thread-safety.md is only verifiable
    // against the callback actually running, and RealtimeSanitizer observes
    // code that executes. Until something drove this function, a green
    // real-time check meant nothing.
    //
    // One named type gets in, rather than widening the class's surface.
    friend struct AudioInputServiceCallbackAccess;

    // The same bargain for recovery. What this file has to answer is "what
    // happens when the device never opens", and a device that never opens
    // cannot be arranged from a test -- so the step that opens it is
    // replaceable, by one named type and nothing else.
    friend struct AudioInputServiceRecoveryAccess;

    static constexpr std::size_t maximumConsumers = 8;
    static constexpr std::size_t consumerFifoCapacity = 65536;
    static constexpr int serviceRefreshRateHz = 20;
    static constexpr int disconnectedDeviceScanIntervalTicks = serviceRefreshRateHz * 2;
    static constexpr int connectedDeviceScanIntervalTicks = serviceRefreshRateHz * 15;

    struct ConsumerSlot
    {
        AudioSampleFifo<consumerFifoCapacity> fifo;
        Listener* listener = nullptr;
        std::atomic<bool> active{false};
    };

    // noexcept and non-blocking are load-bearing, not decoration: see
    // RealtimeSafety.h. Overriding with a stricter exception specification than
    // JUCE's base declares is legal and is what makes the annotation possible.
    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext&) noexcept PRACTICE_TAKES_NONBLOCKING override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    [[nodiscard]] ConsumerSlot* findConsumer(Listener* listener);
    [[nodiscard]] const ConsumerSlot* findConsumer(Listener* listener) const;
    [[nodiscard]] std::array<Listener*, maximumConsumers> listenerSnapshot() const;
    void deliverFormatChange();
    void scanForDeviceChanges();

    // Closing the device and opening it again: the one step of a recovery that
    // can wait without limit. `snd_pcm_prepare` through PipeWire waits on a
    // condition variable that another holder of the device never signals, and
    // there is no timeout to reach.
    //
    // Behind a seam so a test can supply one that blocks. Nothing else can
    // answer "what does this do when the device never opens", and that is the
    // question this file has to have an answer to.
    static void reopenDevice(juce::AudioDeviceManager& deviceManager);

    // One recovery: the flag, the step, the flag again. Separate from the scan
    // that decides to call it, because the deciding needs a device backend to
    // enumerate and this does not -- and this is the part that is about to move
    // to a thread of its own.
    void attemptRecovery();

    // Whether a recovery is still running. The scan asks before starting
    // another: what makes an open block is another holder of the device, which
    // a second attempt does not change -- it only queues a wait on what the
    // first is waiting on.
    [[nodiscard]] bool recoveryInFlight() const;
    void initialiseInput(const juce::XmlElement* state, bool force);
    void publishState();

    // Held through a shared pointer, and this is load-bearing rather than
    // style. A recovery stuck inside `snd_pcm_prepare` cannot be joined at
    // shutdown -- waiting for it would reproduce the freeze this exists to fix,
    // at the moment somebody is trying to escape it -- so the thread outlives
    // this object, and the manager it is inside has to outlive it too. The
    // thread holds a copy; the manager goes when the last one does.
    //
    // As a by-value member this was a use-after-free waiting for a slow device.
    std::shared_ptr<juce::AudioDeviceManager> ownedManager =
        std::make_shared<juce::AudioDeviceManager>();
    juce::AudioDeviceManager& manager = *ownedManager;
    mutable juce::CriticalSection consumerLock;
    std::array<ConsumerSlot, maximumConsumers> consumers;
    juce::String lastDeviceName;
    InputState lastState = InputState::disconnected;

    std::atomic<unsigned int> callbacksInProgress{0};
    std::atomic<bool> muted{false};

    // A synthetic tone, for verification. Zero means the device's own input.
    //
    // Generated in the audio callback rather than pushed from a timer, so it
    // travels the same path a microphone does -- one producer per FIFO, the
    // same gain, the same peak measurement -- and a screenshot of a tool
    // analysing it is a screenshot of the tool working. Read from a table
    // computed once, because the callback may not do unbounded work.
    // A scratch block big enough for any buffer size a device will ask for.
    static constexpr std::size_t maximumToneBlock = 8192;

    std::atomic<float> toneFrequency{0.0f};
    std::atomic<float> toneAmplitude{0.45f};
    SyntheticTone tone;
    std::vector<float> toneBlock;
    std::atomic<float> gain{1.0f};
    std::atomic<float> peakSinceLastTimer{0.0f};
    std::atomic<float> displayedInputLevel{0.0f};
    std::atomic<bool> clippingDetected{false};
    std::atomic<bool> deviceRunning{false};
    std::atomic<double> currentSampleRate{44100.0};
    std::atomic<int> currentInputChannels{0};
    std::atomic<std::uint64_t> formatVersion{0};

    std::uint64_t deliveredFormatVersion = 0;
    std::uint64_t lastReportedDroppedBlocks = 0;
    int clippingHoldTicks = 0;
    int ticksUntilDeviceScan = disconnectedDeviceScanIntervalTicks;
    bool initialised = false;

    // A recovery in flight. Shared with the thread running it, because that
    // thread may still be inside the device when this object is gone: it
    // reports through here and touches nothing else that could have gone.
    struct Flight
    {
        std::atomic<bool> running{true};
    };

    std::shared_ptr<Flight> flight;

    // What a recovery does when it gets as far as the device. Replaced by tests
    // with something that blocks, because a real device that never opens cannot
    // be arranged on demand.
    // Captures the manager's owner, never `this`: the thread running it may
    // outlive this object.
    std::function<void()> reopen = [owner = ownedManager] { reopenDevice(*owner); };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioInputService)
};
