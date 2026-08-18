#pragma once

#include "AudioInputService.h"
#include "PitchDetector.h"

#include <array>
#include <atomic>

// The one place pitch detection runs. Before this existed, the tuner and the
// harmonic analyzer each owned a PitchDetector and each drained their own
// copy of the microphone stream to feed it -- duplicating the expensive step
// (an FFT-based autocorrelation) and, because the two drains were never
// phase-aligned, sometimes disagreeing about the same instant's pitch.
//
// Registers exactly one AudioInputService::Listener regardless of how many
// tools consume the result: it is the only FIFO-backed pitch consumer.
// Consumers read latestResult() on their own timer rather than being pushed
// an update, which is enough for the two tools this exists for today (see
// openspec/changes/share-pitch-analysis/design.md) -- neither needs to react
// to a new pitch reading faster than its own refresh rate already samples.
class SharedPitchAnalysis final : private AudioInputService::Listener, private juce::Timer
{
  public:
    explicit SharedPitchAnalysis(AudioInputService& sharedAudioInputService);
    ~SharedPitchAnalysis() override;

    // The most recent detection. frequency is 0 when nothing has been
    // detected yet, the window was silent, or no signal was pitched -- callers
    // already treat that as "no signal" the same way a direct PitchDetector
    // result would be treated.
    [[nodiscard]] PitchDetector::Result latestResult() const noexcept;

  private:
    // The timer is private because nothing but the timer queue may fire it --
    // a verification harness is the one exception, the same bargain
    // AudioInputServiceCallbackAccess makes for the audio callback (see
    // AudioInputService.h). Waiting on a real 20 Hz timer in a test is slow
    // and, under load, flaky; this lets a test call the one tick it needs.
    friend struct SharedPitchAnalysisTestAccess;

    static constexpr int fifoCapacity = 65536;
    static constexpr int analysisWindowSize = PitchDetector::windowSize;
    static constexpr int refreshRateHz = 20;

    void audioInputAboutToStart(double sampleRate, int inputChannels) override;
    void audioInputStopped() override;
    void audioInputStateChanged(AudioInputService::InputState state) override;
    void timerCallback() override;
    [[nodiscard]] bool drainAudioFifo();

    AudioInputService& audioInputService;
    PitchDetector pitchDetector;

    std::array<float, fifoCapacity> drainBuffer{};
    std::array<float, analysisWindowSize> analysisBuffer{};
    std::atomic<double> currentSampleRate{44100.0};

    std::atomic<double> latestFrequency{0.0};
    std::atomic<float> latestInputLevel{0.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SharedPitchAnalysis)
};
