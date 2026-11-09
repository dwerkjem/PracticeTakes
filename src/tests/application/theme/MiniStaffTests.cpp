#include <catch2/catch_test_macros.hpp>

#include "application/localisation/Localisation.h"
#include "application/theme/MiniStaff.h"

#include <array>
#include <string>
#include <vector>

namespace
{
const juce::Colour lineColour(0xffff0000);
const juce::Colour noteColour(0xff00ff00);

[[nodiscard]] score::Pitch pitchOf(score::Step step, int octave, int alter = 0)
{
    score::Pitch pitch;
    pitch.step = step;
    pitch.octave = octave;
    pitch.alter = alter;
    return pitch;
}

// A staff drawn into a 100 x 140 image: spacing 10, the middle line on row 70,
// the five lines on rows 50 to 90.
[[nodiscard]] juce::Image drawn(std::span<const MiniStaffNote> notes, MiniStaffStyle style = {})
{
    style.lines = lineColour;
    juce::Image image(juce::Image::ARGB, 100, 140, true);
    juce::Graphics graphics(image);
    drawMiniStaff(
        graphics, juce::Rectangle<float>(0.0f, 0.0f, 100.0f, 140.0f), pitch::StaffClef::treble,
        notes, style);
    return image;
}
} // namespace

TEST_CASE("a mini staff names its clef", "[theme][ministaff]")
{
    REQUIRE(l10n::setLanguage("en"));
    CHECK(miniStaffClefName(pitch::StaffClef::treble) == "Treble");
    CHECK(miniStaffClefName(pitch::StaffClef::bass) == "Bass");
    CHECK(miniStaffClefName(pitch::StaffClef::trebleOctaveDown) == "Tenor");
}

TEST_CASE(
    "a mini staff's spacing follows its height inside the style's clamp",
    "[theme][ministaff]")
{
    const MiniStaffStyle style;
    CHECK(miniStaffSpacing(juce::Rectangle<float>(0.0f, 0.0f, 50.0f, 112.0f), style) == 8.0f);
    CHECK(miniStaffSpacing(juce::Rectangle<float>(0.0f, 0.0f, 50.0f, 20.0f), style) == 5.0f);
    CHECK(miniStaffSpacing(juce::Rectangle<float>(0.0f, 0.0f, 50.0f, 900.0f), style) == 10.0f);

    MiniStaffStyle fixed;
    fixed.minSpacing = 8.0f;
    fixed.maxSpacing = 8.0f;
    CHECK(miniStaffSpacing(juce::Rectangle<float>(0.0f, 0.0f, 50.0f, 900.0f), fixed) == 8.0f);
}

TEST_CASE("ledger lines run from the staff out to the note", "[theme][ministaff]")
{
    CHECK(miniStaffLedgerSteps(0).empty());
    CHECK(miniStaffLedgerSteps(5).empty());
    CHECK(miniStaffLedgerSteps(-5).empty());
    CHECK(miniStaffLedgerSteps(6) == std::vector<int>{6});
    CHECK(miniStaffLedgerSteps(7) == std::vector<int>{6});
    CHECK(miniStaffLedgerSteps(9) == std::vector<int>{6, 8});
    CHECK(miniStaffLedgerSteps(-8) == std::vector<int>{-6, -8});
}

TEST_CASE("a mini staff draws its five lines across the inset area", "[theme][ministaff]")
{
    const juce::Image image = drawn({});
    for (const int row : {50, 60, 70, 80, 90})
    {
        CHECK(image.getPixelAt(50, row) == lineColour);
        CHECK(image.getPixelAt(4, row) == lineColour);
        CHECK(image.getPixelAt(2, row).getAlpha() == 0);
    }
    CHECK(image.getPixelAt(50, 55).getAlpha() == 0);
}

TEST_CASE(
    "middle C on a treble mini staff takes a ledger line and a filled head",
    "[theme][ministaff]")
{
    // Six steps below B4: row 100, one ledger line ten pixels either side.
    const std::array notes{MiniStaffNote{pitchOf(score::Step::c, 4), 50.0f, noteColour, true}};
    const juce::Image image = drawn(notes);
    CHECK(image.getPixelAt(50, 100) == noteColour);
    CHECK(image.getPixelAt(41, 100) == lineColour);
    CHECK(image.getPixelAt(30, 100).getAlpha() == 0);
}

TEST_CASE("ledger lines can take the note's colour", "[theme][ministaff]")
{
    MiniStaffStyle style;
    style.ledgersInNoteColour = true;
    const std::array notes{MiniStaffNote{pitchOf(score::Step::c, 4), 50.0f, noteColour, true}};
    const juce::Image image = drawn(notes, style);
    CHECK(image.getPixelAt(41, 100) == noteColour);
}

TEST_CASE("a hollow head leaves its middle empty", "[theme][ministaff]")
{
    // A4 sits in the space below the middle line: row 75.
    const std::array notes{MiniStaffNote{pitchOf(score::Step::a, 4), 50.0f, noteColour, false}};
    const juce::Image image = drawn(notes);
    CHECK(image.getPixelAt(50, 75).getAlpha() == 0);
    CHECK(image.getPixelAt(43, 75).getGreen() > 0);
}

TEST_CASE("a point on a mini staff finds its line or space", "[theme][ministaff]")
{
    // Spacing 10, the middle line on row 70: a step is five pixels.
    const juce::Rectangle<float> area(0.0f, 0.0f, 100.0f, 140.0f);
    const MiniStaffStyle style;
    CHECK(miniStaffStepsAt(area, style, 70.0f) == 0);
    CHECK(miniStaffStepsAt(area, style, 64.0f) == 1);
    CHECK(miniStaffStepsAt(area, style, 90.0f) == -4);
    CHECK(miniStaffStepsAt(area, style, 41.0f) == 6);
}

TEST_CASE("a mini staff with a font makes room for its clef", "[theme][ministaff]")
{
    const juce::ScopedJuceInitialiser_GUI runtime;
    const juce::Rectangle<float> area(0.0f, 0.0f, 200.0f, 140.0f);
    MiniStaffStyle style;
    CHECK(miniStaffClefWidth(area, style) == 0.0f);

    const MusicFont font;
    style.font = &font;
    // A treble clef is about two and a half spaces wide; the room is that, its
    // indent, and clearance for a sharp beside the first head.
    const float width = miniStaffClefWidth(area, style);
    CHECK(width > 50.0f);
    CHECK(width < 75.0f);
}
