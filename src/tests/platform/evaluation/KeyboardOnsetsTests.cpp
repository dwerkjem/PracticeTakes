#include <catch2/catch_test_macros.hpp>

#include "platform/evaluation/KeyboardOnsets.h"

using namespace evaluation;

namespace
{
[[nodiscard]] take::KeyboardEvent key(std::int64_t frame, int note, int velocity, bool on = true)
{
    take::KeyboardEvent event;
    event.frame = frame;
    event.event.kind = on ? midi::MidiEventKind::noteOn : midi::MidiEventKind::noteOff;
    event.event.noteNumber = note;
    event.event.velocity = velocity;
    return event;
}
} // namespace

TEST_CASE("note-ons are onsets, and a chord is one", "[evaluation][keyboard]")
{
    const std::vector<take::KeyboardEvent> events{
        key(1000, 60, 64), key(1300, 64, 127), key(1100, 67, 80), key(5000, 60, 0, false),
        key(20000, 62, 50)};

    const auto onsets = keyboardOnsetsOf(events, 44100.0);
    REQUIRE(onsets.size() == 2);

    // The chord, at its first key, holding all three, as loud as its loudest.
    CHECK(onsets.at(0).frame == 1000);
    CHECK(onsets.at(0).pitches.size() == 3);
    CHECK(onsets.at(0).strength == 1.0);
    CHECK(onsets.at(1).frame == 20000);
    CHECK(onsets.at(1).pitches == std::vector<int>{62});
}

TEST_CASE("a note-on of velocity zero is a release, not an onset", "[evaluation][keyboard]")
{
    CHECK(keyboardOnsetsOf({key(1000, 60, 0)}, 44100.0).empty());
}
