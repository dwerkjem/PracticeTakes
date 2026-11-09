#include <catch2/catch_test_macros.hpp>

#include "application/localisation/NoteNames.h"

using score::NoteNaming;
using score::OctaveNumbering;
using score::PitchNamingStyle;

namespace
{
// Every test leaves the naming as it found it: the default style.
struct NamingAs
{
    explicit NamingAs(PitchNamingStyle style)
    {
        l10n::setNoteNaming(style);
    }

    ~NamingAs()
    {
        l10n::setNoteNaming({});
    }

    NamingAs(const NamingAs&) = delete;
    NamingAs& operator=(const NamingAs&) = delete;
};
} // namespace

TEST_CASE("a heard note is named across the octave and its boundaries", "[localisation][naming]")
{
    CHECK(l10n::heardNoteName(69) == "A4");
    CHECK(l10n::heardNoteName(60) == "C4");
    CHECK(l10n::heardNoteName(61) == "C#4");
    CHECK(l10n::heardNoteName(59) == "B3");
    CHECK(l10n::heardNoteName(12) == "C0");
    CHECK(l10n::heardNoteName(0) == "C-1");
    CHECK(l10n::heardNoteName(200) == "--");
}

TEST_CASE("a flat-key piece's heard notes are spelled with flats", "[localisation][naming]")
{
    // F minor: the tuner says A flat, not G sharp, as the score spells it.
    CHECK(l10n::heardNoteName(68, -4) == "Ab4");
    CHECK(l10n::heardNoteName(70, -4) == "Bb4");
}

TEST_CASE("names follow the reader's style, not the language", "[localisation][naming]")
{
    REQUIRE(l10n::setLanguage("en"));
    const NamingAs german(PitchNamingStyle{NoteNaming::german, OctaveNumbering::scientific});

    CHECK(l10n::heardNoteName(59) == "H3");
    CHECK(l10n::heardNoteName(70, -1) == "B4");
    // A stored target, "Bb3", is shown in the reader's style.
    CHECK(l10n::noteName(std::string_view("Bb3")) == "B3");
    // What is not a note name is shown as it is.
    CHECK(l10n::noteName(std::string_view("--")) == "--");

    l10n::setNoteNaming(PitchNamingStyle{NoteNaming::englishDutch, OctaveNumbering::helmholtz});
    CHECK(l10n::heardNoteName(60) == juce::String::fromUTF8("c\xE2\x80\xB2"));
}
