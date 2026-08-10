#pragma once

// Modular JUCE includes rather than the generated JuceHeader.h umbrella:
// JuceHeader.h is produced for the PracticeTakes application target only, so a
// file that uses it cannot be compiled into PracticeTakesTests. That is the
// mechanical reason this service had no tests.
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "AudioSampleFifo.h"
#include "SyntheticTone.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
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

    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    [[nodiscard]] ConsumerSlot* findConsumer(Listener* listener);
    [[nodiscard]] const ConsumerSlot* findConsumer(Listener* listener) const;
    [[nodiscard]] std::array<Listener*, maximumConsumers> listenerSnapshot() const;
    void deliverFormatChange();
    void scanForDeviceChanges();
    void initialiseInput(const juce::XmlElement* state, bool force);
    void publishState();

    juce::AudioDeviceManager manager;
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
    bool recovering = false;
    bool initialised = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioInputService)
};
