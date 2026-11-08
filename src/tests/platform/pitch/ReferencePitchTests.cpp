#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "platform/pitch/ReferencePitch.h"

#include <limits>

TEST_CASE("the reference is A440 until set, and stays in range", "[pitch][reference]")
{
    pitch::ReferencePitch reference;
    CHECK(reference.hertz() == 440.0);

    reference.set(442.0);
    CHECK(reference.hertz() == 442.0);
    reference.set(400.0);
    CHECK(reference.hertz() == 415.0);
    reference.set(500.0);
    CHECK(reference.hertz() == 466.0);
    reference.set(441.6);
    CHECK(reference.hertz() == 442.0);
    reference.set(std::numeric_limits<double>::quiet_NaN());
    CHECK(reference.hertz() == 440.0);
}

TEST_CASE("a frequency is a note against the reference", "[pitch][reference]")
{
    CHECK(pitch::frequencyToMidi(440.0, 440.0) == Catch::Approx(69.0));
    CHECK(pitch::frequencyToMidi(442.0, 442.0) == Catch::Approx(69.0));

    // A440 against A442 is about eight cents flat.
    CHECK(pitch::frequencyToMidi(440.0, 442.0) == Catch::Approx(68.9215).margin(1e-3));
    CHECK(pitch::midiToFrequency(57.0, 442.0) == Catch::Approx(221.0));
}
