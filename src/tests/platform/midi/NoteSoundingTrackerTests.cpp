#include <catch2/catch_test_macros.hpp>

#include "platform/midi/NoteSoundingTracker.h"

#include <vector>

using namespace midi;

namespace
{
[[nodiscard]] MidiEvent noteOn(int note, double at, int channel = 1, int velocity = 80)
{
    MidiEvent event;
    event.kind = MidiEventKind::noteOn;
    event.channel = channel;
    event.noteNumber = note;
    event.velocity = velocity;
    event.deviceTimestampSeconds = at;
    return event;
}

[[nodiscard]] MidiEvent noteOff(int note, double at, int channel = 1)
{
    MidiEvent event;
    event.kind = MidiEventKind::noteOff;
    event.channel = channel;
    event.noteNumber = note;
    event.deviceTimestampSeconds = at;
    return event;
}

[[nodiscard]] MidiEvent pedal(int position, double at, int channel = 1)
{
    MidiEvent event;
    event.kind = MidiEventKind::sustainPedal;
    event.channel = channel;
    event.pedalPosition = position;
    event.deviceTimestampSeconds = at;
    return event;
}

// A stream, fed in order.
struct Run
{
    NoteSoundingTracker tracker;
    std::vector<SoundedNote> finished;

    void feed(const MidiEvent& event)
    {
        tracker.consume(event, finished);
    }
};
} // namespace

TEST_CASE("pedal up at release: the note sounds as long as the key was held", "[midi][tracker]")
{
    Run run;
    run.feed(pedal(0, 0.0)); // the pedal is known to be up
    run.feed(noteOn(60, 1.0));
    run.feed(noteOff(60, 2.0));

    REQUIRE(run.finished.size() == 1);
    const SoundedNote& note = run.finished[0];
    CHECK(note.end == SoundingEnd::finger);
    CHECK(note.pedal == PedalCertainty::known);
    CHECK(note.fingerSeconds() == 1.0);
    CHECK(note.soundingSeconds() == 1.0);
    CHECK_FALSE(note.heldByPedal());
    CHECK(run.tracker.openNoteCount() == 0);
}

TEST_CASE("released early but held by the pedal", "[midi][tracker]")
{
    Run run;
    run.feed(pedal(127, 0.0));
    run.feed(noteOn(60, 1.0));
    run.feed(noteOff(60, 2.0));

    // The key is up, the pedal is not: nothing has ended yet.
    CHECK(run.finished.empty());
    CHECK(run.tracker.openNoteCount() == 1);

    run.feed(pedal(0, 4.0));

    REQUIRE(run.finished.size() == 1);
    const SoundedNote& note = run.finished[0];
    CHECK(note.end == SoundingEnd::pedal);
    CHECK(note.fingerSeconds() == 1.0);
    CHECK(note.soundingSeconds() == 3.0); // 1s of finger + 2s the pedal added
    CHECK(note.heldByPedal());
    CHECK(run.tracker.openNoteCount() == 0);
}

TEST_CASE("a pedal pressed after the key came up does not extend the note", "[midi][tracker]")
{
    // The design's one deliberate departure from physics: the damper has already
    // fallen. Pinned so that changing it is a visible diff.
    Run run;
    run.feed(pedal(0, 0.0));
    run.feed(noteOn(60, 1.0));
    run.feed(noteOff(60, 2.0));
    run.feed(pedal(127, 2.5));
    run.feed(pedal(0, 5.0));

    REQUIRE(run.finished.size() == 1);
    CHECK(run.finished[0].end == SoundingEnd::finger);
    CHECK(run.finished[0].soundingSeconds() == 1.0);
}

TEST_CASE("a pedal that stays down holds every released note until it comes up", "[midi][tracker]")
{
    Run run;
    run.feed(pedal(127, 0.0));
    run.feed(noteOn(60, 1.0));
    run.feed(noteOff(60, 1.5));
    run.feed(noteOn(64, 2.0));
    run.feed(noteOff(64, 2.5));
    run.feed(pedal(0, 6.0));

    REQUIRE(run.finished.size() == 2);
    for (const SoundedNote& note : run.finished)
    {
        CHECK(note.end == SoundingEnd::pedal);
        CHECK(note.soundingEndSeconds == 6.0);
    }
}

TEST_CASE("a key still held when the pedal comes up keeps sounding", "[midi][tracker]")
{
    // The finger is holding it, so the pedal coming up changes nothing.
    Run run;
    run.feed(pedal(127, 0.0));
    run.feed(noteOn(60, 1.0));
    run.feed(pedal(0, 2.0));

    CHECK(run.finished.empty());
    CHECK(run.tracker.openNoteCount() == 1);

    run.feed(noteOff(60, 3.0));

    REQUIRE(run.finished.size() == 1);
    CHECK(run.finished[0].end == SoundingEnd::finger);
    CHECK(run.finished[0].soundingSeconds() == 2.0);
}

TEST_CASE("re-striking a sustained note ends the previous sound", "[midi][tracker]")
{
    SECTION("the earlier note was only held by the pedal")
    {
        Run run;
        run.feed(pedal(127, 0.0));
        run.feed(noteOn(60, 1.0));
        run.feed(noteOff(60, 2.0));
        run.feed(noteOn(60, 3.0));

        REQUIRE(run.finished.size() == 1);
        const SoundedNote& first = run.finished[0];
        CHECK(first.end == SoundingEnd::restruck);
        CHECK(first.keyUpSeconds == 2.0);
        CHECK(first.soundingEndSeconds == 3.0);
        CHECK(first.heldByPedal());
        CHECK(run.tracker.openNoteCount() == 1); // the new strike
    }

    SECTION("the earlier note's key was somehow still down")
    {
        Run run;
        run.feed(pedal(0, 0.0));
        run.feed(noteOn(60, 1.0));
        run.feed(noteOn(60, 2.0)); // a missing note off

        REQUIRE(run.finished.size() == 1);
        CHECK(run.finished[0].end == SoundingEnd::restruck);
        CHECK(run.finished[0].soundingSeconds() == 1.0);
        CHECK_FALSE(run.finished[0].heldByPedal());
    }
}

TEST_CASE("pedal channels are independent", "[midi][tracker]")
{
    Run run;
    run.feed(pedal(127, 0.0, 1));
    run.feed(pedal(0, 0.0, 2));
    run.feed(noteOn(60, 1.0, 2));
    run.feed(noteOff(60, 2.0, 2));

    // Channel 1's pedal is down; channel 2's note is not sustained by it.
    REQUIRE(run.finished.size() == 1);
    CHECK(run.finished[0].channel == 2);
    CHECK(run.finished[0].end == SoundingEnd::finger);
}

TEST_CASE("a pedal release on one channel does not end another channel's notes", "[midi][tracker]")
{
    Run run;
    run.feed(pedal(127, 0.0, 1));
    run.feed(pedal(127, 0.0, 2));
    run.feed(noteOn(60, 1.0, 1));
    run.feed(noteOff(60, 2.0, 1));
    run.feed(pedal(0, 3.0, 2));

    CHECK(run.finished.empty());
    CHECK(run.tracker.openNoteCount() == 1);
}

TEST_CASE("the pedal counts as down at the threshold, not only at 127", "[midi][tracker]")
{
    // The crossing moved here from translation: the position travels intact and
    // the setting is applied at the decision.
    const auto sustainedAt = [](int position, int threshold)
    {
        Run run;
        run.tracker.setPedalThreshold(threshold);
        run.feed(pedal(position, 0.0));
        run.feed(noteOn(60, 1.0));
        run.feed(noteOff(60, 2.0));
        return run.finished.empty(); // still waiting on the pedal
    };

    CHECK_FALSE(sustainedAt(63, 64));
    CHECK(sustainedAt(64, 64));
    CHECK(sustainedAt(65, 64));
    CHECK(sustainedAt(127, 64));
    CHECK_FALSE(sustainedAt(0, 64));
}

TEST_CASE("the same stream gives a different answer under a different threshold", "[midi][tracker]")
{
    const auto sustainedAt = [](int threshold)
    {
        Run run;
        run.tracker.setPedalThreshold(threshold);
        run.feed(pedal(80, 0.0)); // a half-pedal
        run.feed(noteOn(60, 1.0));
        run.feed(noteOff(60, 2.0));
        return run.finished.empty();
    };

    CHECK(sustainedAt(64));
    CHECK_FALSE(sustainedAt(100));
}

TEST_CASE(
    "a threshold changed after the pedal was seen applies to the next decision",
    "[midi][tracker]")
{
    // The reason `pedalDown` was removed from the event: a boolean frozen at
    // capture time cannot honour a setting changed afterwards. Every other
    // threshold test sets it on a fresh tracker; this one changes it between the
    // pedal event and the note off, which is what the slider does.
    Run run;
    run.feed(pedal(80, 0.0)); // down under the default of 64
    run.feed(noteOn(60, 1.0));

    run.tracker.setPedalThreshold(100); // ... and up under 100

    run.feed(noteOff(60, 2.0));

    REQUIRE(run.finished.size() == 1);
    CHECK(run.finished[0].end == SoundingEnd::finger);
    CHECK_FALSE(run.finished[0].heldByPedal());
}

TEST_CASE("a half-pedal that stays above the threshold keeps the note sounding", "[midi][tracker]")
{
    Run run;
    run.feed(pedal(127, 0.0));
    run.feed(noteOn(60, 1.0));
    run.feed(noteOff(60, 2.0));
    run.feed(pedal(90, 3.0)); // eased off, but still down

    CHECK(run.finished.empty());

    run.feed(pedal(30, 4.0));

    REQUIRE(run.finished.size() == 1);
    CHECK(run.finished[0].soundingEndSeconds == 4.0);
}

TEST_CASE("the threshold is clamped to the range the setting accepts", "[midi][tracker]")
{
    NoteSoundingTracker tracker;

    tracker.setPedalThreshold(0);
    CHECK(tracker.pedalThreshold() == 1);

    tracker.setPedalThreshold(-5);
    CHECK(tracker.pedalThreshold() == 1);

    tracker.setPedalThreshold(200);
    CHECK(tracker.pedalThreshold() == 127);

    tracker.setPedalThreshold(64);
    CHECK(tracker.pedalThreshold() == 64);
}

TEST_CASE("a note released before the pedal is known is flagged, not guessed", "[midi][tracker]")
{
    Run run;
    run.feed(noteOn(60, 1.0));
    run.feed(noteOff(60, 2.0));

    REQUIRE(run.finished.size() == 1);
    CHECK(run.finished[0].pedal == PedalCertainty::unknown);
    CHECK(run.finished[0].soundingEndSeconds == run.finished[0].keyUpSeconds);
}

TEST_CASE("the first pedal event on a channel makes its state known", "[midi][tracker]")
{
    Run run;
    run.feed(pedal(0, 0.0)); // a pedal-up with no prior pedal-down
    run.feed(noteOn(60, 1.0));
    run.feed(noteOff(60, 2.0));

    REQUIRE(run.finished.size() == 1);
    CHECK(run.finished[0].pedal == PedalCertainty::known);
}

TEST_CASE("lost events end every open note as abandoned", "[midi][tracker]")
{
    Run run;
    run.feed(pedal(127, 0.0));
    run.feed(noteOn(60, 1.0)); // key still down
    run.feed(noteOn(64, 1.0));
    run.feed(noteOff(64, 1.5)); // waiting on the pedal

    run.tracker.invalidate(3.0, run.finished);

    REQUIRE(run.finished.size() == 2);
    for (const SoundedNote& note : run.finished)
    {
        CHECK(note.end == SoundingEnd::abandoned);
        CHECK(note.pedal == PedalCertainty::unknown);
        CHECK(note.soundingEndSeconds == 3.0);
    }
    CHECK(run.tracker.openNoteCount() == 0);
}

TEST_CASE("invalidating makes the pedal unknown again", "[midi][tracker]")
{
    Run run;
    run.feed(pedal(127, 0.0));
    run.tracker.invalidate(1.0, run.finished);
    run.finished.clear();

    run.feed(noteOn(60, 2.0));
    run.feed(noteOff(60, 3.0));

    // Without this a lost pedal-up would leave the pedal "down" for ever.
    REQUIRE(run.finished.size() == 1);
    CHECK(run.finished[0].pedal == PedalCertainty::unknown);
}

TEST_CASE(
    "invalidating with nothing held produces nothing, and twice is the same as once",
    "[midi][tracker]")
{
    Run run;
    run.tracker.invalidate(1.0, run.finished);
    CHECK(run.finished.empty());

    run.feed(pedal(0, 2.0));
    run.feed(noteOn(60, 3.0));
    run.tracker.invalidate(4.0, run.finished);
    run.tracker.invalidate(5.0, run.finished);

    REQUIRE(run.finished.size() == 1);
    CHECK(run.finished[0].soundingEndSeconds == 4.0);
}

TEST_CASE("a stream that makes no sense is input, not an error", "[midi][tracker]")
{
    SECTION("a note off with no note on")
    {
        Run run;
        run.feed(pedal(0, 0.0));
        run.feed(noteOff(60, 1.0));

        CHECK(run.finished.empty());
        CHECK(run.tracker.openNoteCount() == 0);
    }

    SECTION("a duplicate note off for a note waiting on the pedal")
    {
        Run run;
        run.feed(pedal(127, 0.0));
        run.feed(noteOn(60, 1.0));
        run.feed(noteOff(60, 2.0));
        run.feed(noteOff(60, 2.5));

        CHECK(run.finished.empty());
        CHECK(run.tracker.openNoteCount() == 1);
    }

    SECTION("an event outside the channel and note range")
    {
        Run run;
        run.feed(noteOn(60, 1.0, 0));
        run.feed(noteOn(60, 1.0, 17));
        run.feed(noteOn(-1, 1.0));
        run.feed(noteOn(128, 1.0));
        run.feed(pedal(127, 1.0, 0));

        CHECK(run.finished.empty());
        CHECK(run.tracker.openNoteCount() == 0);
    }
}

TEST_CASE("each note carries what it was struck with", "[midi][tracker]")
{
    Run run;
    run.feed(pedal(0, 0.0));
    run.feed(noteOn(72, 1.0, 3, 101));
    run.feed(noteOff(72, 1.5, 3));

    // Channel 3's pedal was never seen, so this one is unknown -- but the
    // identity of the note is still reported.
    REQUIRE(run.finished.size() == 1);
    CHECK(run.finished[0].channel == 3);
    CHECK(run.finished[0].noteNumber == 72);
    CHECK(run.finished[0].velocity == 101);
    CHECK(run.finished[0].keyDownSeconds == 1.0);
}
