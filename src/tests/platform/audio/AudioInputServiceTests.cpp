#include <catch2/catch_test_macros.hpp>

#include "platform/audio/AudioInputService.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <vector>

// The first test to invoke the real-time audio callback.
//
// It exists because RealtimeSanitizer observes code that runs, and nothing ran
// this function: `AudioInputService.cpp` was not compiled into
// PracticeTakesTests, and no test referenced it. A real-time check against a
// callback nobody calls reports success and means nothing.
//
// Deliberately narrow. What it covers is the documented callback contract --
// output cleared, mute honoured, captured samples reaching each active
// consumer's FIFO with gain applied. Device lifecycle, level metering, dropout
// accounting, and recovery stay for #116; this is the slice that makes the
// contract observable, not the coverage work.

// Granted access by a friend declaration in AudioInputService.h. The callback
// is private because only a device may call it; a verification harness is the
// one exception, and it is named rather than implied.
struct AudioInputServiceCallbackAccess
{
    static void invoke(
        AudioInputService& service,
        const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples)
    {
        const juce::AudioIODeviceCallbackContext context{};
        service.audioDeviceIOCallbackWithContext(
            inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples,
            context);
    }
};

namespace
{
constexpr int blockSize = 64;

// AudioInputService is a juce::Timer and a juce::ChangeBroadcaster, and both
// assert without a MessageManager. Constructing the service in a bare test
// process produced a wall of JUCE assertion failures and leaked singletons at
// exit -- the tests passed through the noise, which is its own hazard.
struct ServiceUnderTest
{
    // Declaration order is the point: the runtime is constructed before the
    // service and destroyed after it, so the service never exists without a
    // MessageManager underneath it. The unique_ptr is declared second, so the
    // service is destroyed first and never outlives the MessageManager.
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    // On the heap, and it has to be. AudioInputService carries eight inline
    // 65,536-sample FIFOs -- a little over 2 MB -- and Windows gives a thread
    // 1 MB of stack by default where Linux and macOS give 8. As a by-value
    // member this fixture overflowed the stack, and all six cases below
    // segfaulted on Windows while passing everywhere else. The application
    // never had the problem: MainComponent owns the service as a member of a
    // heap-allocated component.
    std::unique_ptr<AudioInputService> owned = std::make_unique<AudioInputService>();
    AudioInputService& service = *owned;
};

// A consumer that only needs to exist. What it receives is read back through
// the service's own readSamples, which is the path a real tool uses.
class SilentConsumer final : public AudioInputService::Listener
{
  public:
    void audioInputAboutToStart(double, int) override {}
    void audioInputStopped() override {}
    void audioInputStateChanged(AudioInputService::InputState) override {}
};

// One input channel of a constant value, and one output channel prefilled with
// something the callback must overwrite.
struct Buffers
{
    std::vector<float> input = std::vector<float>(blockSize, 0.5f);
    std::vector<float> output = std::vector<float>(blockSize, 1.0f);

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

void drive(AudioInputService& service, Buffers& buffers, int numSamples = blockSize)
{
    AudioInputServiceCallbackAccess::invoke(
        service, buffers.inputChannels(), 1, buffers.outputChannels(), 1, numSamples);
}
} // namespace

TEST_CASE("the audio callback clears the output it was handed", "[audio][callback]")
{
    // A capture-only application must not leave whatever was in the output
    // buffer to be played back out of the speakers.
    ServiceUnderTest fixture;
    AudioInputService& service = fixture.service;
    Buffers buffers;

    drive(service, buffers);

    CHECK(std::all_of(
        buffers.output.begin(), buffers.output.end(), [](float sample) { return sample == 0.0f; }));
}

TEST_CASE("the audio callback delivers captured samples to an active consumer", "[audio][callback]")
{
    ServiceUnderTest fixture;
    AudioInputService& service = fixture.service;
    SilentConsumer consumer;
    service.addListener(&consumer);

    Buffers buffers;
    drive(service, buffers);

    CHECK(service.availableSamples(&consumer) == static_cast<std::size_t>(blockSize));

    std::vector<float> drained(blockSize, 0.0f);
    const auto read = service.readSamples(&consumer, drained.data(), drained.size());

    CHECK(read == static_cast<std::size_t>(blockSize));
    CHECK(std::all_of(drained.begin(), drained.end(), [](float sample) { return sample == 0.5f; }));

    service.removeListener(&consumer);
}

TEST_CASE("the audio callback applies the input gain on the way to the FIFO", "[audio][callback]")
{
    ServiceUnderTest fixture;
    AudioInputService& service = fixture.service;
    SilentConsumer consumer;
    service.addListener(&consumer);
    service.setInputGain(2.0f);

    Buffers buffers;
    drive(service, buffers);

    std::vector<float> drained(blockSize, 0.0f);
    const auto read = service.readSamples(&consumer, drained.data(), drained.size());

    CHECK(read == static_cast<std::size_t>(blockSize));
    CHECK(std::all_of(drained.begin(), drained.end(), [](float sample) { return sample == 1.0f; }));

    service.removeListener(&consumer);
}

TEST_CASE("a muted callback still clears the output but captures nothing", "[audio][callback]")
{
    // Mute must not merely zero the analysis: it must stop samples reaching the
    // FIFO at all, or a tool keeps analysing a signal the user believes is off.
    ServiceUnderTest fixture;
    AudioInputService& service = fixture.service;
    SilentConsumer consumer;
    service.addListener(&consumer);
    service.setMuted(true);

    Buffers buffers;
    drive(service, buffers);

    CHECK(service.availableSamples(&consumer) == 0u);
    CHECK(std::all_of(
        buffers.output.begin(), buffers.output.end(), [](float sample) { return sample == 0.0f; }));

    service.removeListener(&consumer);
}

TEST_CASE("a zero-sample callback is a no-op rather than an error", "[audio][callback]")
{
    ServiceUnderTest fixture;
    AudioInputService& service = fixture.service;
    SilentConsumer consumer;
    service.addListener(&consumer);

    Buffers buffers;
    drive(service, buffers, 0);

    CHECK(service.availableSamples(&consumer) == 0u);

    service.removeListener(&consumer);
}

TEST_CASE("a callback with no input channels captures nothing", "[audio][callback]")
{
    // A device that opens output-only hands the callback null input pointers.
    ServiceUnderTest fixture;
    AudioInputService& service = fixture.service;
    SilentConsumer consumer;
    service.addListener(&consumer);

    Buffers buffers;
    AudioInputServiceCallbackAccess::invoke(
        service, nullptr, 0, buffers.outputChannels(), 1, blockSize);

    CHECK(service.availableSamples(&consumer) == 0u);
    CHECK(std::all_of(
        buffers.output.begin(), buffers.output.end(), [](float sample) { return sample == 0.0f; }));

    service.removeListener(&consumer);
}
