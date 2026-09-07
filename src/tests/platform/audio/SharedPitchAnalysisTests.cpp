#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "platform/audio/SharedPitchAnalysis.h"
#include "support/AudioInputServiceCallbackAccess.h"

#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

// Granted access by a friend declaration in SharedPitchAnalysis.h, the same
// bargain AudioInputServiceCallbackAccess (support/AudioInputServiceCallbackAccess.h)
// makes for the audio callback: only a verification harness may fire the
// timer directly rather than waiting on the real one.
struct SharedPitchAnalysisTestAccess
{
    static void runTimerTick(SharedPitchAnalysis& analysis)
    {
        analysis.timerCallback();
    }

    static void
    notifyStateChanged(SharedPitchAnalysis& analysis, AudioInputService::InputState state)
    {
        analysis.audioInputStateChanged(state);
    }
};

namespace
{
constexpr int blockSize = 64;
constexpr double sampleRate = 44100.0;

// SharedPitchAnalysis is a juce::Timer, and AudioInputService is a juce::Timer
// and a juce::ChangeBroadcaster -- both assert without a MessageManager. See
// the identical note on ServiceUnderTest in AudioInputServiceTests.cpp.
struct AnalysisUnderTest
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    std::unique_ptr<AudioInputService> ownedAudioService = std::make_unique<AudioInputService>();
    AudioInputService& audioService = *ownedAudioService;
    std::unique_ptr<SharedPitchAnalysis> ownedAnalysis =
        std::make_unique<SharedPitchAnalysis>(audioService);
    SharedPitchAnalysis& analysis = *ownedAnalysis;
};

struct Buffers
{
    std::vector<float> input = std::vector<float>(blockSize, 0.0f);
    std::vector<float> output = std::vector<float>(blockSize, 0.0f);

    [[nodiscard]] const float* const* inputChannels()
    {
        inputPointers[0] = input.data();
        return inputPointers.data();
    }

    [[nodiscard]] float* const* outputChannels()
    {
        outputPointers[0] = output.data();
        return outputPointers.data();
    }

  private:
    std::array<const float*, 1> inputPointers{};
    std::array<float*, 1> outputPointers{};
};

// Feeds one continuous-phase sine wave through the real audio callback in
// blockSize chunks -- the same path a device would use -- until at least one
// full analysis window is buffered.
void feedTone(AudioInputService& service, double frequency, int totalSamples)
{
    double phase = 0.0;
    const auto phaseStep = 2.0 * std::numbers::pi * frequency / sampleRate;

    for (int delivered = 0; delivered < totalSamples; delivered += blockSize)
    {
        Buffers buffers;
        for (auto& sample : buffers.input)
        {
            sample = 0.5f * static_cast<float>(std::sin(phase));
            phase += phaseStep;
        }

        AudioInputServiceCallbackAccess::invoke(
            service, buffers.inputChannels(), 1, buffers.outputChannels(), 1, blockSize);
    }
}
} // namespace

TEST_CASE("shared pitch analysis reports no signal before any audio arrives", "[audio][pitch]")
{
    AnalysisUnderTest fixture;

    const auto result = fixture.analysis.latestResult();

    CHECK(result.frequency == 0.0);
    CHECK(result.inputLevel == 0.0f);
}

TEST_CASE("shared pitch analysis detects the fundamental of a fed tone", "[audio][pitch]")
{
    AnalysisUnderTest fixture;

    // A full window plus a little headroom, so the newest complete window the
    // drain keeps is entirely tone rather than the buffer's initial silence.
    feedTone(fixture.audioService, 220.0, PitchDetector::windowSize + (blockSize * 4));
    SharedPitchAnalysisTestAccess::runTimerTick(fixture.analysis);

    const auto result = fixture.analysis.latestResult();

    CHECK(result.frequency == Catch::Approx(220.0).margin(0.5));
    CHECK(result.inputLevel > 0.0f);
}

TEST_CASE(
    "shared pitch analysis clears its result when the microphone state changes",
    "[audio][pitch]")
{
    AnalysisUnderTest fixture;

    feedTone(fixture.audioService, 220.0, PitchDetector::windowSize + (blockSize * 4));
    SharedPitchAnalysisTestAccess::runTimerTick(fixture.analysis);
    REQUIRE(fixture.analysis.latestResult().frequency > 0.0);

    SharedPitchAnalysisTestAccess::notifyStateChanged(
        fixture.analysis, AudioInputService::InputState::disconnected);

    const auto result = fixture.analysis.latestResult();
    CHECK(result.frequency == 0.0);
    CHECK(result.inputLevel == 0.0f);
}
