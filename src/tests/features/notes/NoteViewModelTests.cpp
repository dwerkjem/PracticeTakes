#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "features/notes/NoteViewModel.h"

using Catch::Approx;
using namespace notes;

TEST_CASE("keys sit where a keyboard puts them", "[notes][keys]")
{
    CHECK_FALSE(isBlackKey(60));
    CHECK(isBlackKey(61));
    CHECK(isBlackKey(70));
    CHECK_FALSE(isBlackKey(64));

    // C4 is the 29th white key from C-1; its C# sits on the C-D boundary.
    CHECK(keyCentre(60) == Approx(35.5));
    CHECK(keyCentre(61) == Approx(36.0));
    CHECK(keyCentre(62) == Approx(36.5));
    CHECK(keyCentre(64) - keyCentre(65) == Approx(-1.0));
}

TEST_CASE("a pitch between two keys lies between them", "[notes][keys]")
{
    CHECK(pitchX(60.0) == Approx(keyCentre(60)));
    CHECK(pitchX(60.5) == Approx((keyCentre(60) + keyCentre(61)) / 2.0));
    CHECK(pitchX(64.5) == Approx((keyCentre(64) + keyCentre(65)) / 2.0));

    // A slide from G4 to A4 moves steadily rightwards.
    double previous = pitchX(67.0);
    for (double midi = 67.1; midi <= 69.0; midi += 0.1)
    {
        CHECK(pitchX(midi) > previous);
        previous = pitchX(midi);
    }
}

TEST_CASE("the keys hold still until a note leaves them", "[notes][keys]")
{
    const KeyWindow start;
    CHECK(windowFor(start, {}) == start);
    CHECK(windowFor(start, {64, 72}) == start);

    // A low note moves the window down to its C; a wide span widens it.
    const KeyWindow low = windowFor(start, {43});
    CHECK(low.lowest == 36);
    CHECK(low.highest == 60);
    const KeyWindow wide = windowFor(start, {40, 90});
    CHECK(wide.lowest == 36);
    CHECK(wide.highest == 96);
}

TEST_CASE("the sung note's zone is against the target, or else its nearest note", "[notes]")
{
    using pitch::IntonationZone;

    // No pitch, no zone.
    CHECK_FALSE(currentZone(std::nullopt, 0.0, 69, 25.0).has_value());

    // Against the target A4: near perfect, acceptable, near but off, outside --
    // out of tune past 40 cents by default.
    CHECK(currentZone(69.03, 3.0, 69, 25.0) == IntonationZone::nearPerfect);
    CHECK(currentZone(68.85, -15.0, 69, 25.0) == IntonationZone::acceptable);
    CHECK(currentZone(69.3, 30.0, 69, 25.0) == IntonationZone::nearButOff);
    CHECK(currentZone(69.45, 45.0, 69, 25.0) == IntonationZone::outside);
    CHECK(currentZone(69.45, 45.0, 69, 25.0, 5.0, 50.0) == IntonationZone::nearButOff);

    // In tune on B4, but the target is A4: outside.
    CHECK(currentZone(71.0, 0.0, 69, 25.0) == IntonationZone::outside);

    // Without a target, against the note it is nearest.
    CHECK(currentZone(71.12, 12.0, std::nullopt, 25.0) == IntonationZone::acceptable);
    CHECK(currentZone(71.12, 12.0, std::nullopt, 10.0) == IntonationZone::nearButOff);
}

TEST_CASE("the trail keeps three seconds, with its gaps", "[notes][trail]")
{
    Trail trail;
    for (int frame = 0; frame <= 40; ++frame)
    {
        const double seconds = frame * 0.1;
        trail.add(seconds, frame == 20 ? std::nullopt : std::optional<double>(60.0 + frame * 0.05));
    }
    const auto& points = trail.recent();
    CHECK(points.front().seconds >= 1.0 - 1e-9);
    CHECK(points.back().seconds == Approx(4.0));
    CHECK_FALSE(points[static_cast<std::size_t>(10)].midi.has_value());
}

TEST_CASE("a point on the keyboard finds the key under it", "[notes][keyboard]")
{
    // C4 to C6: fifteen white keys, so each is a fifteenth of the width.
    const notes::KeyWindow window{60, 84};
    const double white = 1.0 / 15.0;

    // Low on the keys only white keys are there.
    CHECK(notes::keyAt(window, white * 0.5, 0.9) == 60);
    CHECK(notes::keyAt(window, white * 1.5, 0.9) == 62);
    CHECK(notes::keyAt(window, 1.0 - (white * 0.5), 0.9) == 84);

    // High up, a black key covers the boundary between its neighbours.
    CHECK(notes::keyAt(window, white * 1.0, 0.3) == 61);
    CHECK(notes::keyAt(window, white * 1.0, 0.9) != 61);
    // Between E and F there is no black key, so the white one is found.
    CHECK(notes::keyAt(window, white * 2.9, 0.3) == 64);

    // Off the keys, nothing.
    CHECK_FALSE(notes::keyAt(window, -0.01, 0.5).has_value());
    CHECK_FALSE(notes::keyAt(window, 0.5, 1.2).has_value());
}
