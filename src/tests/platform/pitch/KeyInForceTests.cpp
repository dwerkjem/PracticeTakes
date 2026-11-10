#include <catch2/catch_test_macros.hpp>

#include "platform/pitch/KeyInForce.h"

using namespace score;

namespace
{
// Three bars of 4/4: C major, then G major from bar 2.
[[nodiscard]] Score modulating()
{
    Score song;
    Part part;
    for (int bar = 0; bar < 3; ++bar)
    {
        Measure measure;
        measure.start = bar * ticksPerWholeNote;
        measure.nominalDuration = ticksPerWholeNote;
        if (bar == 0)
        {
            measure.attributes.key = KeySignature{0};
        }
        if (bar == 1)
        {
            measure.attributes.key = KeySignature{1};
        }
        part.measures.push_back(measure);
    }
    song.parts.push_back(part);
    return song;
}
} // namespace

TEST_CASE("the key in force is the measure's, carried from its last change", "[pitch][key]")
{
    const Score song = modulating();
    CHECK(pitch::keyFifthsAtMeasure(song.parts[0], 0) == 0);
    CHECK(pitch::keyFifthsAtMeasure(song.parts[0], 1) == 1);
    // No change in bar 3, so G major carries on.
    CHECK(pitch::keyFifthsAtMeasure(song.parts[0], 2) == 1);
    // A part that names no key is in C.
    CHECK(pitch::keyFifthsAtMeasure(Part{}, 5) == 0);
}

TEST_CASE("the key in force follows the target, else the clock, else the reader", "[pitch][key]")
{
    const Score song = modulating();

    // No score: the reader's own key.
    CHECK(pitch::keyInForce(nullptr, nullptr, 0.0, -2) == -2);

    // The clock in bar 1, then bar 3.
    CHECK(pitch::keyInForce(&song, nullptr, 100.0, -2) == 0);
    CHECK(pitch::keyInForce(&song, nullptr, 2.5 * ticksPerWholeNote, -2) == 1);
    // Counting in, before the music: the first bar's.
    CHECK(pitch::keyInForce(&song, nullptr, -400.0, -2) == 0);

    // A target in bar 2 is in G major, wherever the clock is -- an A natural
    // there shows one sharp.
    pitch::Target aimed;
    aimed.place = pitch::Target::ScorePlace{0, NoteRef{1, 0, 0, 0}};
    CHECK(pitch::keyInForce(&song, &aimed, 0.0, -2) == 1);

    // A target from a key, with no note of the score, leaves it to the clock.
    pitch::Target fromKey;
    CHECK(pitch::keyInForce(&song, &fromKey, 0.0, -2) == 0);
}
