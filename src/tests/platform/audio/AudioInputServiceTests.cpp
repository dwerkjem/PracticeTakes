#include <catch2/catch_test_macros.hpp>

#include "platform/audio/AudioInputService.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <functional>
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

// The recovery's one blocking step, replaceable. Named rather than implied, for
// the same reason as the callback above: this is a verification harness reaching
// into something private, and the class says which harness.
struct AudioInputServiceRecoveryAccess
{
    static void replaceReopen(AudioInputService& service, std::function<void()> step)
    {
        service.reopen = std::move(step);
    }

    // The recovery, not the scan that decides on one. Driving the scan makes
    // the device backends enumerate, and the timers they create outlive the
    // fixture's message manager -- a wall of JUCE assertions the real
    // assertions then pass through.
    static void attempt(AudioInputService& service)
    {
        service.attemptRecovery();
    }

    static bool recovering(const AudioInputService& service)
    {
        return service.recoveryInFlight();
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

// --- Recovery ---------------------------------------------------------------
//
// Run these three together in one process -- `PracticeTakesTests "[recovery]"`
// -- and JUCE reports "a timer has outlived the platform event system" a few
// times on the way out. It is the fixture cycling `ScopedJuceInitialiser_GUI`
// once per case, not these cases: each alone is silent, any two are silent, and
// ctest gives every case its own process, which is how CI and the suite run
// them. Written down because the file above already records what noise like
// this costs -- real assertions pass through it unread.
//
// What these pin is the shape of a recovery, not whether one is wanted --
// `AudioRecoveryPolicy` decides that and is tested next door. The shape is
// what is about to change: today the reopen happens on the message thread,
// which is why a device that never opens freezes the interface.

TEST_CASE("a recovery does not make the caller wait for the device", "[audio][recovery]")
{
    // The whole change, in one case: the step blocks, and asking for a recovery
    // returns anyway. On the message thread this is the difference between a
    // busy device and an application that cannot be used or closed.
    ServiceUnderTest harness;
    juce::WaitableEvent released;
    juce::WaitableEvent entered;

    AudioInputServiceRecoveryAccess::replaceReopen(
        harness.service,
        [&entered, &released]
        {
            entered.signal();
            released.wait();
        });

    const auto before = juce::Time::getMillisecondCounterHiRes();
    AudioInputServiceRecoveryAccess::attempt(harness.service);
    const auto waited = juce::Time::getMillisecondCounterHiRes() - before;

    REQUIRE(entered.wait(5000));
    REQUIRE(waited < 1000.0);
    REQUIRE(AudioInputServiceRecoveryAccess::recovering(harness.service));

    released.signal();
}

TEST_CASE("a second recovery is not started while one is running", "[audio][recovery]")
{
    // The scan runs every two seconds while there is no usable input, which is
    // exactly the state a stuck open leaves it in. Without this, a slow device
    // produces a queue of attempts, each waiting on what the one before waits
    // on.
    ServiceUnderTest harness;
    juce::WaitableEvent released;
    juce::WaitableEvent entered;
    std::atomic<int> attempts{0};

    AudioInputServiceRecoveryAccess::replaceReopen(
        harness.service,
        [&entered, &released, &attempts]
        {
            ++attempts;
            entered.signal();
            released.wait();
        });

    AudioInputServiceRecoveryAccess::attempt(harness.service);
    REQUIRE(entered.wait(5000));

    AudioInputServiceRecoveryAccess::attempt(harness.service);
    AudioInputServiceRecoveryAccess::attempt(harness.service);

    REQUIRE(attempts.load() == 1);

    released.signal();
}

TEST_CASE("a recovery that finishes lets the next one start", "[audio][recovery]")
{
    ServiceUnderTest harness;
    std::atomic<int> attempts{0};
    AudioInputServiceRecoveryAccess::replaceReopen(harness.service, [&attempts] { ++attempts; });

    AudioInputServiceRecoveryAccess::attempt(harness.service);

    for (auto waited = 0;
         waited < 5000 && AudioInputServiceRecoveryAccess::recovering(harness.service);
         waited += 10)
    {
        juce::Thread::sleep(10);
    }

    REQUIRE_FALSE(AudioInputServiceRecoveryAccess::recovering(harness.service));

    AudioInputServiceRecoveryAccess::attempt(harness.service);

    for (auto waited = 0; waited < 5000 && attempts.load() < 2; waited += 10)
    {
        juce::Thread::sleep(10);
    }

    REQUIRE(attempts.load() == 2);
}

TEST_CASE("the input state says the device is being opened", "[audio][recovery]")
{
    // "No microphone detected" and "the microphone is not answering" are
    // different situations with different answers, and only one of them reads
    // as the application having crashed.
    ServiceUnderTest harness;
    juce::WaitableEvent released;
    juce::WaitableEvent entered;

    REQUIRE(harness.service.inputState() == AudioInputService::InputState::disconnected);

    AudioInputServiceRecoveryAccess::replaceReopen(
        harness.service,
        [&entered, &released]
        {
            entered.signal();
            released.wait();
        });
    AudioInputServiceRecoveryAccess::attempt(harness.service);
    REQUIRE(entered.wait(5000));

    REQUIRE(harness.service.inputState() == AudioInputService::InputState::opening);

    released.signal();
}

TEST_CASE("the service can be destroyed while a recovery is stuck", "[audio][recovery]")
{
    // The case that has to work, or quitting reproduces the freeze at the
    // moment somebody is trying to escape it. The step here never returns
    // until the test lets it, which is what a device held by something else
    // looks like from in here.
    //
    // Run this under AddressSanitizer: the mistake it guards against -- a
    // thread still inside a manager that has been destroyed -- is invisible
    // without one.
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    juce::WaitableEvent released;
    juce::WaitableEvent entered;
    std::atomic<bool> finished{false};

    {
        auto service = std::make_unique<AudioInputService>();
        AudioInputServiceRecoveryAccess::replaceReopen(
            *service,
            [&entered, &released, &finished]
            {
                entered.signal();
                released.wait();
                finished = true;
            });
        AudioInputServiceRecoveryAccess::attempt(*service);
        REQUIRE(entered.wait(5000));

        const auto before = juce::Time::getMillisecondCounterHiRes();
        service.reset();
        const auto waited = juce::Time::getMillisecondCounterHiRes() - before;

        REQUIRE(waited < 2000.0);
        REQUIRE_FALSE(finished.load());
    }

    // Let the abandoned recovery finish, and give it a moment to run out
    // against a manager its own copy is keeping alive.
    released.signal();

    for (auto waited = 0; waited < 5000 && !finished.load(); waited += 10)
    {
        juce::Thread::sleep(10);
    }

    REQUIRE(finished.load());
}

// --- The tone without a device ----------------------------------------------
//
// The tone is never heard. It exists so a tool has something to analyse for a
// capture, and until now it was generated inside the audio callback -- so it
// needed a device it had no other use for. One process at a time holds that
// device, which serialised every surface carrying a tone onto whichever one
// won it.

TEST_CASE("a tone with no device still reaches a tool", "[audio][tone]")
{
    ServiceUnderTest harness;
    SilentConsumer consumer;
    harness.service.addListener(&consumer);

    // Nothing is open here: a bare test process has no device running, which is
    // exactly the situation a capture worker without one is in.
    REQUIRE_FALSE(harness.service.hasUsableInput());

    harness.service.setSyntheticTone(440.0f);

    std::vector<float> heard(4096, 0.0f);
    std::size_t got = 0;

    for (auto waited = 0; waited < 3000 && got == 0; waited += 20)
    {
        juce::Thread::sleep(20);
        got = harness.service.readSamples(&consumer, heard.data(), heard.size());
    }

    harness.service.setSyntheticTone(0.0f);
    harness.service.removeListener(&consumer);

    REQUIRE(got > 0);
    REQUIRE(std::any_of(
        heard.begin(), heard.begin() + static_cast<long>(got),
        [](float sample) { return std::abs(sample) > 0.001f; }));
}

TEST_CASE("a tone with no device reports as input rather than as missing", "[audio][tone]")
{
    ServiceUnderTest harness;

    REQUIRE(harness.service.inputState() == AudioInputService::InputState::disconnected);

    harness.service.setSyntheticTone(440.0f);

    for (auto waited = 0; waited < 2000 && !harness.service.hasUsableInput(); waited += 20)
    {
        juce::Thread::sleep(20);
    }

    const auto state = harness.service.inputState();
    harness.service.setSyntheticTone(0.0f);

    REQUIRE(harness.service.hasUsableInput() == false);
    REQUIRE(state == AudioInputService::InputState::active);
}

TEST_CASE("clearing the tone stops the generator", "[audio][tone]")
{
    ServiceUnderTest harness;
    harness.service.setSyntheticTone(440.0f);

    for (auto waited = 0; waited < 2000 && !harness.service.hasUsableInput(); waited += 20)
    {
        juce::Thread::sleep(20);
    }

    REQUIRE(harness.service.hasUsableInput());

    harness.service.setSyntheticTone(0.0f);

    REQUIRE_FALSE(harness.service.hasUsableInput());
}
