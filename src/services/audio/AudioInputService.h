#pragma once

#include <JuceHeader.h>

#include "AudioSampleFifo.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

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

    [[nodiscard]] float inputGain() const noexcept;
    void setInputGain(float gain);
    [[nodiscard]] float inputLevel() const noexcept;
    [[nodiscard]] std::uint64_t droppedAnalysisBlocks() const noexcept;
    [[nodiscard]] std::uint64_t droppedAnalysisSamples() const noexcept;

    void resetToDefaultInput();
    void applySavedDeviceState(const juce::XmlElement& state);
    [[nodiscard]] std::unique_ptr<juce::XmlElement> createDeviceState() const;

  private:
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
    void publishState();

    juce::AudioDeviceManager manager;
    mutable juce::CriticalSection consumerLock;
    std::array<ConsumerSlot, maximumConsumers> consumers;
    juce::String lastDeviceName;
    InputState lastState = InputState::disconnected;

    std::atomic<unsigned int> callbacksInProgress{0};
    std::atomic<bool> muted{false};
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioInputService)
};
