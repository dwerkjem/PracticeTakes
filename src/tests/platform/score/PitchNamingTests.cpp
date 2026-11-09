#include <catch2/catch_test_macros.hpp>

#include "platform/pitch/TargetChoice.h"
#include "platform/score/PitchNames.h"
#include "platform/score/PitchNaming.h"

using score::NoteNaming;
using score::OctaveNumbering;
using score::Pitch;
using score::PitchNamingStyle;
using score::Step;

namespace
{
[[nodiscard]] Pitch spelled(Step step, int alter, int octave)
{
    return score::pitchFromSpelling(step, alter, octave);
}

[[nodiscard]] std::string named(const Pitch& pitch, NoteNaming naming, OctaveNumbering octaves)
{
    return score::pitchName(pitch, PitchNamingStyle{naming, octaves});
}
} // namespace

TEST_CASE("B and H, either side of the boundary", "[score][naming]")
{
    const Pitch bNatural = spelled(Step::b, 0, 3);
    const Pitch bFlat = spelled(Step::b, -1, 3);
    const Pitch c = spelled(Step::c, 0, 4);

    CHECK(named(bNatural, NoteNaming::englishDutch, OctaveNumbering::scientific) == "B3");
    CHECK(named(bNatural, NoteNaming::german, OctaveNumbering::scientific) == "H3");
    // B flat is spelled B in German, not "Bes".
    CHECK(named(bFlat, NoteNaming::englishDutch, OctaveNumbering::scientific) == "Bb3");
    CHECK(named(bFlat, NoteNaming::german, OctaveNumbering::scientific) == "B3");
    // Across the octave boundary, the number belongs to the C above.
    CHECK(named(c, NoteNaming::german, OctaveNumbering::scientific) == "C4");
}

TEST_CASE("German sharps and flats take -is and -es", "[score][naming]")
{
    const auto german = [](Step step, int alter)
    { return score::pitchClassName(step, alter, NoteNaming::german); };
    CHECK(german(Step::f, 1) == "Fis");
    CHECK(german(Step::c, 2) == "Cisis");
    CHECK(german(Step::d, -1) == "Des");
    CHECK(german(Step::e, -1) == "Es");
    CHECK(german(Step::a, -1) == "As");
    CHECK(german(Step::a, -2) == "Ases");
    CHECK(german(Step::b, 1) == "His");
    CHECK(german(Step::b, -2) == "Heses");
}

TEST_CASE("fixed do names C Do", "[score][naming]")
{
    CHECK(named(spelled(Step::c, 0, 4), NoteNaming::romance, OctaveNumbering::scientific) == "Do4");
    CHECK(
        named(spelled(Step::g, 0, 4), NoteNaming::romance, OctaveNumbering::scientific) == "Sol4");
    CHECK(
        named(spelled(Step::f, 1, 3), NoteNaming::romance, OctaveNumbering::scientific) == "Fa#3");
}

TEST_CASE("middle C in all three octave conventions", "[score][naming]")
{
    const Pitch middleC = spelled(Step::c, 0, 4);
    CHECK(named(middleC, NoteNaming::englishDutch, OctaveNumbering::scientific) == "C4");
    CHECK(named(middleC, NoteNaming::englishDutch, OctaveNumbering::yamaha) == "C3");
    CHECK(named(middleC, NoteNaming::englishDutch, OctaveNumbering::helmholtz) == "c\xE2\x80\xB2");

    // Helmholtz around it: small, great, contra; and two primes above.
    CHECK(
        named(spelled(Step::c, 0, 3), NoteNaming::englishDutch, OctaveNumbering::helmholtz) == "c");
    CHECK(
        named(spelled(Step::c, 0, 2), NoteNaming::englishDutch, OctaveNumbering::helmholtz) == "C");
    CHECK(
        named(spelled(Step::c, 0, 1), NoteNaming::englishDutch, OctaveNumbering::helmholtz) ==
        "C,");
    CHECK(
        named(spelled(Step::c, 0, 5), NoteNaming::englishDutch, OctaveNumbering::helmholtz) ==
        "c\xE2\x80\xB3");
    // German Helmholtz is how a German edition prints it.
    CHECK(
        named(spelled(Step::f, 1, 4), NoteNaming::german, OctaveNumbering::helmholtz) ==
        "fis\xE2\x80\xB2");
}

TEST_CASE("the canonical name is the default style's", "[score][naming]")
{
    for (const Pitch& pitch :
         {spelled(Step::c, 0, -1), spelled(Step::f, 1, 3), spelled(Step::b, -2, 2)})
    {
        CHECK(score::spelledName(pitch) == score::pitchName(pitch));
    }
    CHECK(score::spelledName(spelled(Step::c, 0, -1)) == "C-1");
    CHECK(score::spelledName(spelled(Step::b, -2, 2)) == "Bbb2");
}

TEST_CASE("a heard note is spelled as its key would spell it", "[score][naming][pitch]")
{
    // F minor has four flats: its A flat is A flat, not G sharp.
    const auto inFMinor = pitch::spellForKey(68, -4);
    REQUIRE(inFMinor.has_value());
    CHECK(score::spelledName(*inFMinor) == "Ab4");

    // With no key, or a sharp one, the black keys take sharps.
    CHECK(score::spelledName(*pitch::spellForKey(68, 0)) == "G#4");
    CHECK(score::spelledName(*pitch::spellForKey(68, 3)) == "G#4");
}
