#pragma once

#include <JuceHeader.h>

// Owns the application's only microphone callback and fans the captured input
// out to tools. Tools consume samples; they never open or configure hardware.
class AudioInputService final : public juce::ChangeBroadcaster,
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
        virtual void audioInputAboutToStart(double sampleRate) = 0;
        virtual void audioInputReceived(const float* samples, int numSamples) = 0;
        virtual void audioInputStopped() = 0;
        virtual void audioInputStateChanged(InputState state) = 0;
    };

    AudioInputService();
    ~AudioInputService() override;

    void addListener(Listener* listener);
    void removeListener(Listener* listener);

    [[nodiscard]] juce::AudioDeviceManager& deviceManager() noexcept;
    [[nodiscard]] bool hasUsableInput() const;
    [[nodiscard]] InputState inputState() const;
    [[nodiscard]] bool isMuted() const noexcept;
    void setMuted(bool shouldBeMuted);
    void toggleMuted();
    void resetToDefaultInput();
    void applySavedDeviceState(const juce::XmlElement& state);
    [[nodiscard]] std::unique_ptr<juce::XmlElement> createDeviceState() const;

  private:
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels, float* const* outputChannelData,
                                          int numOutputChannels, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void publishState();

    juce::AudioDeviceManager manager;
    juce::ListenerList<Listener, juce::Array<Listener*, juce::CriticalSection>> listeners;
    juce::String lastDeviceName;
    InputState lastState = InputState::disconnected;
    std::atomic<bool> muted{false};
    std::atomic<bool> clippingDetected{false};
    int clippingHoldTicks = 0;
    bool recovering = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioInputService)
};
