#include <catch2/catch_test_macros.hpp>

#include "platform/pitch/SteadyReading.h"

TEST_CASE("only a note held steadily for long enough is taken", "[pitch][choice]")
{
    pitch::SteadyReading reading;
    CHECK_FALSE(reading.steadyNote(0.0).has_value());

    reading.update(69, 5.0, 0.0);
    CHECK_FALSE(reading.steadyNote(200.0).has_value());
    reading.update(69, -8.0, 250.0);
    CHECK(reading.steadyNote(320.0) == 69);

    // Sliding to another note starts again.
    reading.update(70, 10.0, 400.0);
    CHECK_FALSE(reading.steadyNote(600.0).has_value());
    CHECK(reading.steadyNote(700.0) == 70);

    // Too far from the note, or silence, takes nothing.
    reading.update(70, 42.0, 710.0);
    CHECK_FALSE(reading.steadyNote(2000.0).has_value());
    reading.update(70, 0.0, 2000.0);
    reading.update(std::nullopt, 0.0, 2100.0);
    CHECK_FALSE(reading.steadyNote(3000.0).has_value());
}
