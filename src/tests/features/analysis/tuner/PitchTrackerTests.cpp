#include "features/analysis/tuner/PitchTracker.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace
{
constexpr PitchTracker::Settings defaults{};

// A4 = 440 Hz = MIDI 69, the reference the tracker converts against.
constexpr double a4 = 440.0;

[[nodiscard]] double semitonesAbove(double frequency, double semitones)
{
    return frequency * std::pow(2.0, semitones / 12.0);
}

// Feed the same pitch until the tracker has settled on it, so a test that cares
// about what happens *next* does not also have to care about the ramp-up.
void settle(PitchTracker& tracker, double frequency, int frames = 30)
{
    for (int frame = 0; frame < frames; ++frame)
    {
        (void)tracker.detected(frequency, defaults);
    }
}
} // namespace

TEST_CASE("PitchTracker converts between frequency and MIDI", "[tuner][pitch-tracker]")
{
    CHECK(PitchTracker::frequencyToMidi(a4) == Catch::Approx(69.0));
    CHECK(PitchTracker::midiToFrequency(69.0) == Catch::Approx(a4));

    // An octave is twelve semitones in either direction.
    CHECK(PitchTracker::frequencyToMidi(a4 * 2.0) == Catch::Approx(81.0));
    CHECK(PitchTracker::midiToFrequency(57.0) == Catch::Approx(a4 / 2.0));
}

TEST_CASE("A steady pitch is reported and held", "[tuner][pitch-tracker]")
{
    PitchTracker tracker;
    const auto first = tracker.detected(a4, defaults);

    CHECK(first.accepted);
    CHECK(first.hasSignal);
    CHECK(first.displayedMidiNote == 69);
    CHECK(first.displayedCents == Catch::Approx(0.0).margin(0.5));

    settle(tracker, a4);
    CHECK(tracker.smoothedMidiNote() == Catch::Approx(69.0).margin(0.01));
}

TEST_CASE("Cents report the distance from the displayed note", "[tuner][pitch-tracker]")
{
    PitchTracker tracker;

    SECTION("drifting sharp of a held note reports positive cents")
    {
        // The lock must already be on A4, because the first frame of a fresh
        // tracker locks to whatever is nearest -- at +0.5 that rounds to A#4,
        // and the reading would correctly be -50 cents against that instead.
        settle(tracker, a4);
        settle(tracker, semitonesAbove(a4, 0.5));

        const auto update = tracker.detected(semitonesAbove(a4, 0.5), defaults);
        CHECK(update.displayedMidiNote == 69);
        CHECK(update.displayedCents == Catch::Approx(50.0).margin(2.0));
    }

    SECTION("a fresh tracker locks to the nearest note, not the one below")
    {
        settle(tracker, semitonesAbove(a4, 0.5));

        const auto update = tracker.detected(semitonesAbove(a4, 0.5), defaults);
        CHECK(update.displayedMidiNote == 70);
        CHECK(update.displayedCents == Catch::Approx(-50.0).margin(2.0));
    }
}

TEST_CASE("A large jump is rejected until it repeats", "[tuner][pitch-tracker]")
{
    // The behaviour that makes a tuner usable on a plucked string: the first
    // frames of a note often read an octave high, and following them would make
    // the display leap and then crawl back.
    PitchTracker tracker;
    settle(tracker, a4);

    const auto octaveUp = a4 * 2.0;

    SECTION("a single outlying frame is refused and leaves a gap")
    {
        const auto update = tracker.detected(octaveUp, defaults);
        CHECK_FALSE(update.accepted);
        CHECK(std::isnan(update.historyMidiNote));
        // The smoothed value must not have moved toward the outlier.
        CHECK(tracker.smoothedMidiNote() == Catch::Approx(69.0).margin(0.01));
    }

    SECTION("a jump sustained for enough frames is admitted")
    {
        bool everAccepted = false;
        for (int frame = 0; frame < 8; ++frame)
        {
            everAccepted = tracker.detected(octaveUp, defaults).accepted || everAccepted;
        }
        CHECK(everAccepted);
        CHECK(tracker.smoothedMidiNote() == Catch::Approx(81.0).margin(0.5));
    }

    SECTION("frames that disagree with each other never confirm")
    {
        // Wildly scattered readings are noise, not a glide, however many arrive.
        for (int frame = 0; frame < 12; ++frame)
        {
            const auto scattered = frame % 2 == 0 ? octaveUp : a4 * 3.0;
            CHECK_FALSE(tracker.detected(scattered, defaults).accepted);
        }
        CHECK(tracker.smoothedMidiNote() == Catch::Approx(69.0).margin(0.01));
    }
}

TEST_CASE("A small interval is followed immediately", "[tuner][pitch-tracker]")
{
    // Below the jump threshold there is nothing to confirm; ordinary playing
    // must not be delayed by four frames.
    PitchTracker tracker;
    settle(tracker, a4);

    const auto update = tracker.detected(semitonesAbove(a4, 2.0), defaults);
    CHECK(update.accepted);
    CHECK_FALSE(std::isnan(update.historyMidiNote));
}

TEST_CASE("Silence clears the tracker only after the dropout window", "[tuner][pitch-tracker]")
{
    PitchTracker tracker;
    settle(tracker, a4);

    PitchTracker::Settings settings = defaults;
    settings.dropoutFrames = 3.0;

    SECTION("a brief gap does not discard the note")
    {
        const auto update = tracker.missing(settings);
        CHECK_FALSE(update.accepted);
        CHECK(std::isnan(update.historyMidiNote));
        CHECK(tracker.hasSignal());
        CHECK(tracker.smoothedMidiNote() == Catch::Approx(69.0).margin(0.01));
    }

    SECTION("a sustained gap resets")
    {
        for (int frame = 0; frame < 3; ++frame)
        {
            (void)tracker.missing(settings);
        }
        CHECK_FALSE(tracker.hasSignal());
        CHECK(tracker.smoothedMidiNote() == Catch::Approx(0.0));
    }
}

TEST_CASE("Only missing frames count toward the dropout", "[tuner][pitch-tracker]")
{
    // A rejected frame is not silence. It must neither reset the dropout
    // counter nor advance it -- the counter measures how long the signal has
    // been gone, and a suspected harmonic means the signal is very much there.
    //
    // Pinned because the guard is one early return away from being wrong:
    // moving the counter reset above the confirmation check would let a stream
    // of rejected frames hold the note open forever, and every other test in
    // this file would still pass.
    PitchTracker tracker;
    settle(tracker, a4);

    PitchTracker::Settings settings = defaults;
    settings.dropoutFrames = 3.0;

    const auto octaveUp = a4 * 2.0;

    (void)tracker.missing(settings); // 1 silent frame
    REQUIRE_FALSE(tracker.detected(octaveUp, settings).accepted);
    (void)tracker.missing(settings); // 2 silent frames
    REQUIRE_FALSE(tracker.detected(octaveUp, settings).accepted);

    CHECK(tracker.hasSignal());

    (void)tracker.missing(settings); // 3 silent frames reaches the dropout
    CHECK_FALSE(tracker.hasSignal());
}

TEST_CASE("A rejected or missing frame always leaves a gap in the graph", "[tuner][pitch-tracker]")
{
    // Production reads historyMidiNote and never accepted, so the two must not
    // drift apart: a frame reported accepted while carrying NaN would draw a
    // line straight through a gap, and tests asserting only on `accepted`
    // would not notice.
    PitchTracker tracker;
    PitchTracker::Settings settings = defaults;
    settings.dropoutFrames = 50.0;

    const auto agree = [](const PitchTracker::Update& update)
    { CHECK(update.accepted == !std::isnan(update.historyMidiNote)); };

    agree(tracker.detected(a4, settings));
    for (int frame = 0; frame < 20; ++frame)
    {
        agree(tracker.detected(a4, settings));
        agree(tracker.detected(a4 * 2.0, settings));
        agree(tracker.missing(settings));
    }
}

TEST_CASE("Note lock holds the label until the pitch moves far enough", "[tuner][pitch-tracker]")
{
    // Without hysteresis the label flickers between two notes when the player
    // sits on the boundary between them.
    PitchTracker tracker;
    settle(tracker, a4);

    PitchTracker::Settings settings = defaults;
    settings.noteSwitchSemitones = 0.55;

    SECTION("drifting within the threshold keeps the note")
    {
        settle(tracker, semitonesAbove(a4, 0.4));
        CHECK(tracker.detected(semitonesAbove(a4, 0.4), settings).displayedMidiNote == 69);
    }

    SECTION("moving beyond it changes the note")
    {
        settle(tracker, semitonesAbove(a4, 1.0));
        CHECK(tracker.detected(semitonesAbove(a4, 1.0), settings).displayedMidiNote == 70);
    }
}

TEST_CASE("Easing controls how quickly the display follows", "[tuner][pitch-tracker]")
{
    const auto target = semitonesAbove(a4, 2.0);

    PitchTracker slow;
    PitchTracker fast;
    settle(slow, a4);
    settle(fast, a4);

    PitchTracker::Settings slowSettings = defaults;
    slowSettings.easing = 0.05;
    PitchTracker::Settings fastSettings = defaults;
    fastSettings.easing = 0.9;

    for (int frame = 0; frame < 3; ++frame)
    {
        (void)slow.detected(target, slowSettings);
        (void)fast.detected(target, fastSettings);
    }

    const auto targetMidi = PitchTracker::frequencyToMidi(target);
    CHECK(
        std::abs(fast.smoothedMidiNote() - targetMidi) <
        std::abs(slow.smoothedMidiNote() - targetMidi));
}

TEST_CASE("reset returns the tracker to its initial state", "[tuner][pitch-tracker]")
{
    PitchTracker tracker;
    settle(tracker, a4);
    REQUIRE(tracker.hasSignal());

    tracker.reset();

    CHECK_FALSE(tracker.hasSignal());
    CHECK(tracker.smoothedMidiNote() == Catch::Approx(0.0));

    // A fresh pitch after a reset is adopted immediately rather than being
    // treated as a jump away from the pitch heard before the reset.
    const auto update = tracker.detected(a4 * 2.0, defaults);
    CHECK(update.accepted);
}

TEST_CASE("Note names cover the octave and its boundaries", "[tuner][pitch-tracker]")
{
    CHECK(noteNameForMidi(69) == "A4");
    CHECK(noteNameForMidi(60) == "C4");
    CHECK(noteNameForMidi(61) == "C#4");
    CHECK(noteNameForMidi(59) == "B3");
    CHECK(noteNameForMidi(12) == "C0");
    CHECK(noteNameForMidi(0) == "C-1");
}
