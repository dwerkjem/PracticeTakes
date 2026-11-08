#include <catch2/catch_test_macros.hpp>

#include "platform/audio/AudioInputService.h"
#include "platform/score/Score.h"
#include "platform/transport/ClockRenderer.h"
#include "platform/transport/PlaybackEvents.h"
#include "platform/transport/ScoreSynth.h"
#include "support/AudioInputServiceCallbackAccess.h"

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

    // The scan the timer runs. Safe to drive only on an instance that has
    // declined input -- that is the case it returns from before it reaches the
    // backends, so none of the enumeration the comment above warns about
    // happens. Driving it on an ordinary service would bring the timers back.
    static void scan(AudioInputService& service)
    {
        service.scanForDeviceChanges();
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

    // On the heap, and it has to be. AudioInputService carries sixteen inline
    // 65,536-sample FIFOs -- a little over 4 MB -- and Windows gives a thread
    // 1 MB of stack by default where Linux and macOS give 8. As a by-value
    // member this fixture overflowed the stack, and all six cases below
    // segfaulted on Windows while passing everywhere else. The application
    // never had the problem: MainComponent owns the service as a member of a
    // heap-allocated component.
    std::unique_ptr<AudioInputService> owned;
    AudioInputService& service;

    // `declineInput` builds the service a capture instance gets: one that opens
    // no input device at all. Defaulted, so every case above is unaffected.
    explicit ServiceUnderTest(bool declineInput = false)
        : owned(std::make_unique<AudioInputService>(declineInput)), service(*owned)
    {
    }
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

TEST_CASE(
    "changing the tone while it is already playing does not race the generator",
    "[audio][tone][.load]")
{
    // What the test-control channel actually does: `open-state` calls
    // `setSyntheticTone` every time, whether or not one is already running.
    // Two capture surfaces carrying a tone back to back -- an ordinary
    // sequence, not a contrived one -- call this while the generator's own
    // timer thread is mid-block, writing the same phase fields `reset()`
    // writes. TSan caught the race the first version of this had: no ordering
    // between `tone.reset()` on this thread and `SyntheticTone::advance()` on
    // the timer thread.
    //
    // A single-threaded assertion cannot observe that a race did not happen;
    // what this pins is the shape that exposed it, so a regression is
    // reachable by the tools that can see it. Tagged [.load] to run under
    // ThreadSanitizer rather than every ordinary build.
    ServiceUnderTest harness;

    for (auto frequency = 220.0f; frequency < 240.0f; frequency += 1.0f)
    {
        harness.service.setSyntheticTone(frequency);
        juce::Thread::sleep(1);
    }

    for (auto waited = 0; waited < 2000 && !harness.service.hasUsableInput(); waited += 20)
    {
        juce::Thread::sleep(20);
    }

    REQUIRE(harness.service.hasUsableInput());

    harness.service.setSyntheticTone(0.0f);
}

// --- The audio callback and the tone source, sharing SyntheticTone ---------
//
// A tone is rendered by two different callers depending on whether a device is
// delivering blocks: the ToneSource timer thread when there is none, the audio
// callback itself when there is -- audioDeviceIOCallbackWithContext renders the
// tone whenever one is requested, whether or not a device is present, so a
// device showing up does not make it stop asking. Nothing used to make those
// two callers mutually exclusive except the message thread's own polling of
// updateToneSource(), up to one cycle behind an actual device starting. TSan
// found the same class of race here that it found for setSyntheticTone: two
// threads inside SyntheticTone::render() at once.

TEST_CASE(
    "a device about to start stops the tone source before the first block can arrive",
    "[audio][tone]")
{
    // The fix, pinned directly: this must be true the instant aboutToStart
    // returns, not eventually. Every JUCE backend calls this synchronously
    // before the real-time thread can invoke the callback, so "eventually" was
    // the bug.
    ServiceUnderTest harness;
    harness.service.setSyntheticTone(440.0f);

    for (auto waited = 0;
         waited < 2000 && !AudioInputServiceCallbackAccess::toneSourceRunning(harness.service);
         waited += 20)
    {
        juce::Thread::sleep(20);
    }

    REQUIRE(AudioInputServiceCallbackAccess::toneSourceRunning(harness.service));

    AudioInputServiceCallbackAccess::aboutToStart(harness.service);

    REQUIRE_FALSE(AudioInputServiceCallbackAccess::toneSourceRunning(harness.service));

    AudioInputServiceCallbackAccess::stopped(harness.service);
    harness.service.setSyntheticTone(0.0f);
}

TEST_CASE(
    "a device starting while its tone plays does not race the tone source",
    "[audio][tone][.load]")
{
    // The real sequence, repeated under load rather than asserted once: ask
    // for a tone with no device, let the ToneSource actually start, then
    // simulate a device arriving -- aboutToStart, then real callback
    // invocations spanning several of the ToneSource's own 20ms ticks (not
    // just a handful back to back, which finish in microseconds and stand a
    // poor chance of actually overlapping a periodic timer), audioDeviceStopped
    // -- and do it again immediately, with no pause for the message thread to
    // catch up in between. That gap is exactly the window the original bug
    // lived in; spanning several ticks per cycle, repeated, is what gives
    // ThreadSanitizer a real chance to see a regression rather than a lucky
    // miss.
    ServiceUnderTest harness;
    Buffers buffers;

    for (auto cycle = 0; cycle < 8; ++cycle)
    {
        harness.service.setSyntheticTone(440.0f);

        for (auto waited = 0;
             waited < 2000 && !AudioInputServiceCallbackAccess::toneSourceRunning(harness.service);
             waited += 20)
        {
            juce::Thread::sleep(20);
        }

        REQUIRE(AudioInputServiceCallbackAccess::toneSourceRunning(harness.service));

        // Deliberately no assertion on what aboutToStart leaves behind here --
        // that is the previous test's job, and it fails outright without the
        // fix. This test's job is to reach the concurrent section regardless,
        // so a reverted fix is caught by ThreadSanitizer actually observing the
        // two threads in render() together, not by an assertion that stops the
        // test before it gets there.
        AudioInputServiceCallbackAccess::aboutToStart(harness.service);

        const auto until = juce::Time::getMillisecondCounterHiRes() + 90.0;
        while (juce::Time::getMillisecondCounterHiRes() < until)
        {
            drive(harness.service, buffers);
        }

        AudioInputServiceCallbackAccess::stopped(harness.service);
    }

    harness.service.setSyntheticTone(0.0f);
}

// --- Declining the input device ---------------------------------------------
//
// One process at a time holds the input device, so a verification capture run
// and a live application compete for it and the application loses (#176). A
// capture instance does not want the device it is competing for: the callback
// above already replaces device input with the tone whenever one is asked for,
// so an instance that only ever analyses a tone opens a device, reads from it,
// and discards every sample.
//
// These cases cover the instance that declines one outright.

TEST_CASE("a declined instance opens no device when asked to initialise", "[audio][declined]")
{
    ServiceUnderTest harness{true};

    harness.service.initialiseDefaultInput();

    REQUIRE(harness.service.deviceManager().getCurrentAudioDevice() == nullptr);
    REQUIRE_FALSE(harness.service.hasUsableInput());
    REQUIRE_FALSE(AudioInputServiceRecoveryAccess::recovering(harness.service));
}

TEST_CASE("a declined instance refuses an explicit device request", "[audio][declined]")
{
    ServiceUnderTest harness{true};

    // The two paths a user reaches: the Settings button, and a settings import.
    // Both go through initialiseInput, and both must come back without opening
    // anything and without blocking the caller -- the drop being silent is the
    // right behaviour here, because declining was the instruction.
    harness.service.resetToDefaultInput();

    juce::XmlElement saved{"DEVICESETUP"};
    saved.setAttribute("audioDeviceName", "Some Input");
    harness.service.applySavedDeviceState(saved);

    REQUIRE(harness.service.deviceManager().getCurrentAudioDevice() == nullptr);
    REQUIRE_FALSE(AudioInputServiceRecoveryAccess::recovering(harness.service));
}

TEST_CASE("a declined instance attempts no recovery when its timer scans", "[audio][declined]")
{
    ServiceUnderTest harness{true};

    // The scan would otherwise notice the absent device and queue work that is
    // then refused, once per tick, for the life of the process.
    AudioInputServiceRecoveryAccess::scan(harness.service);
    AudioInputServiceRecoveryAccess::scan(harness.service);

    REQUIRE_FALSE(AudioInputServiceRecoveryAccess::recovering(harness.service));
    REQUIRE(harness.service.deviceManager().getCurrentAudioDevice() == nullptr);
}

TEST_CASE("a declined instance still feeds a tone to a tool", "[audio][declined]")
{
    ServiceUnderTest harness{true};
    SilentConsumer consumer;
    harness.service.addListener(&consumer);

    harness.service.setSyntheticTone(440.0f);

    std::vector<float> heard(4096, 0.0f);
    std::size_t got = 0;

    for (auto waited = 0; waited < 3000 && got == 0; waited += 20)
    {
        juce::Thread::sleep(20);
        got = harness.service.readSamples(&consumer, heard.data(), heard.size());
    }

    const auto reported = harness.service.hasUsableInput();
    const auto state = harness.service.inputState();

    harness.service.setSyntheticTone(0.0f);
    harness.service.removeListener(&consumer);

    REQUIRE(got > 0);
    REQUIRE(std::any_of(
        heard.begin(), heard.begin() + static_cast<long>(got),
        [](float sample) { return std::abs(sample) > 0.001f; }));

    // Being fed a tone is having input, whatever the reason there is no device.
    // A declined instance that drew "microphone disconnected" over a graph of
    // the signal it is being given would be describing its own plumbing.
    REQUIRE(reported);
    REQUIRE(state == AudioInputService::InputState::active);
    REQUIRE(harness.service.deviceManager().getCurrentAudioDevice() == nullptr);
}

TEST_CASE("an ordinary instance is unchanged by the flag existing", "[audio][declined]")
{
    // The default, which is what the application and every other case here
    // builds. Nothing is opened in a bare test process either, but for the
    // ordinary reason -- no device was asked for -- and the scan is still the
    // live code path rather than an early return.
    ServiceUnderTest harness;

    REQUIRE_FALSE(harness.service.hasUsableInput());
    REQUIRE_FALSE(AudioInputServiceRecoveryAccess::recovering(harness.service));
}

TEST_CASE("a declined instance still enumerates the devices it will not open", "[audio][declined]")
{
    // The Settings input-device chooser is a captured surface, and it carries a
    // human instruction to switch input device and switch back. An empty list
    // makes that unanswerable, so declining to *open* a device must not become
    // declining to know which devices exist.
    //
    // `getCurrentDeviceTypeObject()` is the assertion rather than a device-name
    // list, for two reasons: it is a plain read of what enumeration already
    // populated -- it starts no scan of its own, so it cannot manufacture the
    // result it is checking -- and it does not depend on the machine running
    // the tests having a microphone, which a name list would.
    ServiceUnderTest harness{true};

    REQUIRE(harness.service.deviceManager().getCurrentDeviceTypeObject() == nullptr);

    harness.service.initialiseDefaultInput();

    REQUIRE(harness.service.deviceManager().getCurrentDeviceTypeObject() != nullptr);
    REQUIRE(harness.service.deviceManager().getCurrentAudioDevice() == nullptr);
}

TEST_CASE("muting silences the generated tone as well as a device", "[audio][tone][declined]")
{
    // The generator bypasses the audio callback entirely, and the callback is
    // where mute was honoured -- so muting a device-free instance silenced
    // nothing, and a tool went on drawing a reading from a microphone the user
    // had just turned off. Latent until capture instances stopped opening
    // devices; every one of them is device-free now.
    ServiceUnderTest harness{true};
    SilentConsumer consumer;
    harness.service.addListener(&consumer);
    harness.service.setSyntheticTone(440.0f);

    std::vector<float> heard(4096, 0.0f);

    for (auto waited = 0; waited < 3000; waited += 20)
    {
        juce::Thread::sleep(20);

        if (harness.service.readSamples(&consumer, heard.data(), heard.size()) > 0)
        {
            break;
        }
    }

    harness.service.setMuted(true);

    // Settle before discarding, not after. The generator runs on a timer
    // thread, so a block that was already past the mute check when `setMuted`
    // landed still delivers -- discarding first and reading straight away
    // raced that block and failed about half the time.
    juce::Thread::sleep(100);
    harness.service.discardPendingSamples(&consumer);

    // Several times the 20ms block interval, so an unmuted generator would
    // have delivered a dozen blocks into the gap.
    juce::Thread::sleep(300);

    const auto afterMute = harness.service.readSamples(&consumer, heard.data(), heard.size());

    // Still *running*, so the interface can say muted rather than disconnected.
    // Those are different things to a person: one means you turned it off, the
    // other means plug something in.
    const auto state = harness.service.inputState();
    const auto usable = harness.service.hasUsableInput();

    harness.service.setMuted(false);
    harness.service.setSyntheticTone(0.0f);
    harness.service.removeListener(&consumer);

    REQUIRE(afterMute == 0);
    REQUIRE(usable);
    REQUIRE(state == AudioInputService::InputState::muted);
}

TEST_CASE(
    "free consumer slots count down as listeners join and back up as they leave",
    "[audio][consumers]")
{
    // The shell asks this before creating another tool that listens, because
    // addListener has no way to tell its caller the slots are full
    // (openspec/changes/archive/2026-11-08-multi-instance-tools).
    ServiceUnderTest fixture;
    AudioInputService& service = fixture.service;
    const auto initiallyFree = service.freeConsumerSlots();
    REQUIRE(initiallyFree > 2);

    SilentConsumer first;
    SilentConsumer second;
    service.addListener(&first);
    service.addListener(&second);
    CHECK(service.freeConsumerSlots() == initiallyFree - 2);

    // Adding the same listener twice takes no second slot.
    service.addListener(&first);
    CHECK(service.freeConsumerSlots() == initiallyFree - 2);

    service.removeListener(&first);
    CHECK(service.freeConsumerSlots() == initiallyFree - 1);

    service.removeListener(&second);
    CHECK(service.freeConsumerSlots() == initiallyFree);
}

namespace
{
// Records what it is told, so a state-only listener's notifications can be
// checked without a device.
class RecordingStateListener final : public AudioInputService::Listener
{
  public:
    void audioInputAboutToStart(double, int) override
    {
        ++starts;
    }
    void audioInputStopped() override
    {
        ++stops;
    }
    void audioInputStateChanged(AudioInputService::InputState state) override
    {
        states.push_back(state);
    }

    int starts = 0;
    int stops = 0;
    std::vector<AudioInputService::InputState> states;
};
} // namespace

TEST_CASE("a state-only listener takes no consumer slot", "[audio][consumers]")
{
    // The tuner reads pitch from SharedPitchAnalysis and only needs to know
    // when the input is muted or gone, so it must not cost a sample stream.
    ServiceUnderTest fixture;
    AudioInputService& service = fixture.service;
    const auto initiallyFree = service.freeConsumerSlots();

    RecordingStateListener listener;
    service.addStateListener(&listener);
    CHECK(service.freeConsumerSlots() == initiallyFree);

    // Told the current state straight away, as a sample listener is.
    REQUIRE_FALSE(listener.states.empty());
    CHECK(listener.states.back() == service.inputState());

    service.removeStateListener(&listener);
    CHECK(service.freeConsumerSlots() == initiallyFree);
}

TEST_CASE("a state-only listener hears a mute", "[audio][consumers]")
{
    ServiceUnderTest fixture;
    AudioInputService& service = fixture.service;

    // A tone counts as input, which mute can then silence. With no input at
    // all the state is "disconnected", and mute has nothing to change.
    service.setSyntheticTone(440.0f);
    for (auto waited = 0; waited < 2000 && !service.hasUsableInput(); waited += 20)
    {
        juce::Thread::sleep(20);
    }
    REQUIRE(service.hasUsableInput());

    RecordingStateListener listener;
    service.addStateListener(&listener);
    service.addStateListener(&listener); // twice is once
    const auto heardBefore = listener.states.size();
    CHECK(heardBefore == 1);

    // setMuted publishes the new state synchronously on the calling thread.
    service.setMuted(true);

    REQUIRE(listener.states.size() > heardBefore);
    CHECK(listener.states.back() == AudioInputService::InputState::muted);
    service.removeStateListener(&listener);
    service.setMuted(false);
    service.setSyntheticTone(0.0f);
}

TEST_CASE("there are sixteen sample streams", "[audio][consumers]")
{
    CHECK(AudioInputService::consumerCapacity() == 16);
}

namespace
{
// A clock ready to play four bars of 4/4 at 120 bpm: a click every 22,050
// frames, the first at frame 0.
struct PlayingClock
{
    transport::ClockRenderer renderer;

    PlayingClock()
    {
        score::Score source;
        source.tempoMap = score::TempoMap::build({{0, 120.0}});
        score::Part part;
        for (int index = 0; index < 4; ++index)
        {
            score::Measure measure;
            measure.index = static_cast<std::size_t>(index);
            measure.start = static_cast<score::Tick>(index) * score::ticksPerWholeNote;
            measure.nominalDuration = score::ticksPerWholeNote;
            part.measures.push_back(std::move(measure));
        }
        source.parts.push_back(std::move(part));

        auto table = std::make_unique<transport::ClockTable>();
        transport::buildTable(*table, source, 100, 44100.0);
        REQUIRE(renderer.publish(*table));
        REQUIRE(renderer.send({transport::ClockCommand::Kind::play}));
    }
};

// Stands a device up for the callback to run under, so the generator -- which
// runs whenever a renderer is set and no device is -- never calls the
// renderer from a second thread while the test drives the callback.
void standUpDevice(AudioInputService& service)
{
    AudioInputServiceCallbackAccess::aboutToStart(service);
}

[[nodiscard]] bool anySound(const std::vector<float>& samples)
{
    return std::any_of(samples.begin(), samples.end(), [](float sample) { return sample != 0.0f; });
}
} // namespace

TEST_CASE("the audio callback renders the clock into the output", "[audio][callback][transport]")
{
    ServiceUnderTest fixture;
    standUpDevice(fixture.service);
    PlayingClock clock;
    fixture.service.setRealtimeRenderer(&clock.renderer);

    Buffers buffers;
    drive(fixture.service, buffers);

    // The downbeat's click starts at frame 0, so this block holds its attack;
    // everything else in the block was cleared first.
    CHECK(anySound(buffers.output));
    CHECK(clock.renderer.snapshot().position == blockSize);

    fixture.service.setRealtimeRenderer(nullptr);
}

TEST_CASE("a muted audio callback still sounds the metronome", "[audio][callback][transport]")
{
    ServiceUnderTest fixture;
    standUpDevice(fixture.service);
    SilentConsumer consumer;
    fixture.service.addListener(&consumer);
    fixture.service.setMuted(true);
    PlayingClock clock;
    fixture.service.setRealtimeRenderer(&clock.renderer);

    Buffers buffers;
    drive(fixture.service, buffers);

    // Mute silences the microphone, not the click.
    CHECK(anySound(buffers.output));
    CHECK(fixture.service.availableSamples(&consumer) == 0u);

    fixture.service.setRealtimeRenderer(nullptr);
    fixture.service.removeListener(&consumer);
}

TEST_CASE(
    "the audio callback advances the clock with no output open",
    "[audio][callback][transport]")
{
    ServiceUnderTest fixture;
    standUpDevice(fixture.service);
    PlayingClock clock;
    fixture.service.setRealtimeRenderer(&clock.renderer);

    Buffers buffers;
    for (int block = 0; block < 10; ++block)
    {
        AudioInputServiceCallbackAccess::invoke(
            fixture.service, buffers.inputChannels(), 1, nullptr, 0, blockSize);
    }

    CHECK(clock.renderer.snapshot().position == 10 * blockSize);

    fixture.service.setRealtimeRenderer(nullptr);
}

TEST_CASE(
    "the audio callback stops calling a renderer once it is removed",
    "[audio][callback][transport]")
{
    ServiceUnderTest fixture;
    standUpDevice(fixture.service);
    PlayingClock clock;
    fixture.service.setRealtimeRenderer(&clock.renderer);

    Buffers buffers;
    drive(fixture.service, buffers);
    fixture.service.setRealtimeRenderer(nullptr);
    CHECK(fixture.service.realtimeRenderer() == nullptr);

    drive(fixture.service, buffers);
    CHECK(clock.renderer.snapshot().position == blockSize);
    CHECK_FALSE(anySound(buffers.output));
}

TEST_CASE("the generator keeps the clock's time when no device is open", "[audio][tone][transport]")
{
    ServiceUnderTest fixture;
    REQUIRE_FALSE(fixture.service.hasUsableInput());

    PlayingClock clock;
    fixture.service.setRealtimeRenderer(&clock.renderer);
    REQUIRE(AudioInputServiceCallbackAccess::toneSourceRunning(fixture.service));

    for (auto waited = 0; waited < 3000 && clock.renderer.snapshot().position == 0; waited += 20)
    {
        juce::Thread::sleep(20);
    }

    CHECK(clock.renderer.snapshot().position > 0);

    // Running only to keep time, it is not input: no tone was asked for.
    CHECK_FALSE(fixture.service.hasUsableInput());

    fixture.service.setRealtimeRenderer(nullptr);
}

TEST_CASE("the audio callback records a take's input", "[audio][callback][transport][take]")
{
    ServiceUnderTest fixture;
    standUpDevice(fixture.service);
    PlayingClock clock;
    clock.renderer.armRecording();
    fixture.service.setInputGain(2.0f);
    fixture.service.setRealtimeRenderer(&clock.renderer);

    Buffers buffers;
    drive(fixture.service, buffers);

    // A muted block is recorded as silence, so the take's timeline has no gap.
    fixture.service.setMuted(true);
    drive(fixture.service, buffers);

    fixture.service.setRealtimeRenderer(nullptr);

    transport::RecordedBlock block;
    std::vector<float> samples(blockSize);

    REQUIRE(clock.renderer.popRecordedBlock(block));
    CHECK(block.position == 0);
    CHECK(block.frames == static_cast<std::uint32_t>(blockSize));
    REQUIRE(clock.renderer.readRecording(samples.data(), samples.size()) == samples.size());
    CHECK(samples.front() == 1.0f);

    REQUIRE(clock.renderer.popRecordedBlock(block));
    CHECK(block.position == blockSize);
    REQUIRE(clock.renderer.readRecording(samples.data(), samples.size()) == samples.size());
    CHECK(samples.front() == 0.0f);
}

TEST_CASE(
    "the audio callback plays the score without allocating",
    "[audio][callback][transport][playback]")
{
    // Under RealtimeSanitizer this is the check that TinySoundFont, once
    // loaded, allocates nothing on any path the renderer takes: notes on and
    // off on two hands at once, a hand going quiet, a loop's wrap and a seek
    // (openspec/changes/archive/2026-11-08-score-playback, design.md decision 1).
    score::Score source;
    source.tempoMap = score::TempoMap::build({{0, 240.0}});
    score::Part piano;
    piano.staffCount = 2;
    for (int bar = 0; bar < 2; ++bar)
    {
        score::Measure measure;
        measure.index = static_cast<std::size_t>(bar);
        measure.start = static_cast<score::Tick>(bar) * score::ticksPerWholeNote;
        measure.nominalDuration = score::ticksPerWholeNote;
        if (bar == 0)
        {
            measure.attributes.time = score::TimeSignature{4, 4};
        }
        for (int staff = 1; staff <= 2; ++staff)
        {
            score::Voice voice;
            for (int eighth = 0; eighth < 8; ++eighth)
            {
                score::ScoreEvent event;
                event.kind = score::EventKind::note;
                event.onset = static_cast<score::Tick>(eighth) * score::ticksPerQuarterNote / 2;
                event.duration = score::ticksPerQuarterNote / 2;
                event.staff = staff;
                for (const int interval : {0, 4, 7})
                {
                    score::Pitch pitch;
                    pitch.midiNoteNumber = (staff == 1 ? 60 : 36) + eighth + interval;
                    event.notes.push_back(score::Note{pitch, pitch, std::nullopt, std::nullopt});
                }
                voice.events.push_back(event);
            }
            measure.voices.push_back(voice);
        }
        piano.measures.push_back(measure);
    }
    source.parts.push_back(piano);

    // Loaded here, off the callback, as the application loads it.
    const auto synth = transport::ScoreSynth::load(PRACTICE_TAKES_TEST_SOUNDFONT);
    REQUIRE(synth != nullptr);
    transport::ClockRenderer renderer;
    auto table = std::make_unique<transport::ClockTable>();
    transport::buildTable(*table, source, 100, 44100.0, 0, score::ticksPerWholeNote);
    transport::PlaybackScore notes;
    transport::playbackEventsOf(source, *table, notes);
    REQUIRE(renderer.publish(*table, notes));
    renderer.setMetronomeEnabled(false);
    renderer.setSynth(synth.get());
    renderer.setAudibleTracks(0b11);
    REQUIRE(renderer.send({transport::ClockCommand::Kind::play}));

    ServiceUnderTest fixture;
    standUpDevice(fixture.service);
    fixture.service.setRealtimeRenderer(&renderer);

    Buffers buffers;
    bool heard = false;
    for (int block = 0; block < 1500; ++block)
    {
        if (block == 600)
        {
            renderer.setAudibleTracks(0b10);
        }
        if (block == 900)
        {
            REQUIRE(
                renderer.send({transport::ClockCommand::Kind::seek, score::ticksPerQuarterNote}));
        }
        drive(fixture.service, buffers);
        heard = heard || anySound(buffers.output);
    }

    CHECK(heard);
    CHECK(renderer.events().wraps > 0);
    CHECK(renderer.playbackOverflows() == 0);
    fixture.service.setRealtimeRenderer(nullptr);
}
