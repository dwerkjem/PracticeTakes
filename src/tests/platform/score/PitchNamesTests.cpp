#include <catch2/catch_test_macros.hpp>

#include "platform/score/PitchNames.h"

using namespace score;

TEST_CASE("a natural pitch names its step and octave", "[score][pitch]")
{
    CHECK(spelledName(pitchFromSpelling(Step::c, 0, 4)) == "C4");
    CHECK(spelledName(pitchFromSpelling(Step::a, 0, 4)) == "A4");
    CHECK(spelledName(pitchFromSpelling(Step::b, 0, 3)) == "B3");
}

TEST_CASE("single accidentals are spelled with one sign", "[score][pitch]")
{
    CHECK(spelledName(pitchFromSpelling(Step::f, 1, 3)) == "F#3");
    CHECK(spelledName(pitchFromSpelling(Step::e, -1, 5)) == "Eb5");
}

TEST_CASE("double accidentals show both signs", "[score][pitch]")
{
    CHECK(spelledName(pitchFromSpelling(Step::b, -2, 2)) == "Bbb2");
    CHECK(spelledName(pitchFromSpelling(Step::c, 2, 4)) == "C##4");
}

// The whole reason this takes a Pitch rather than a MIDI number. The tuner's
// noteNameForMidi cannot make this distinction, and the model keeps the
// spelling precisely so that it survives.
TEST_CASE("enharmonic spellings of one sounding pitch name differently", "[score][pitch]")
{
    const Pitch fSharp = pitchFromSpelling(Step::f, 1, 4);
    const Pitch gFlat = pitchFromSpelling(Step::g, -1, 4);

    REQUIRE(soundsSameAs(fSharp, gFlat));
    CHECK(spelledName(fSharp) == "F#4");
    CHECK(spelledName(gFlat) == "Gb4");
    CHECK(spelledName(fSharp) != spelledName(gFlat));
}

TEST_CASE("the octave number is the written one, including below zero", "[score][pitch]")
{
    // MIDI 0. The tuner shows "C-1" for it, and one application should spell a
    // note one way.
    CHECK(spelledName(pitchFromSpelling(Step::c, 0, -1)) == "C-1");
    CHECK(spelledName(pitchFromSpelling(Step::c, 0, 0)) == "C0");
}

// B#3 sounds as C4 and is written in the octave below it. A name derived from
// the sounding number would put it in the wrong octave as well as spelling it
// wrong, which is the pair of mistakes this function exists to avoid.
TEST_CASE("a spelling that crosses the octave boundary keeps its own octave", "[score][pitch]")
{
    const Pitch bSharp = pitchFromSpelling(Step::b, 1, 3);

    REQUIRE(bSharp.midiNoteNumber == pitchFromSpelling(Step::c, 0, 4).midiNoteNumber);
    CHECK(spelledName(bSharp) == "B#3");
}

TEST_CASE("an alteration beyond a double is shown rather than clamped", "[score][pitch]")
{
    // The importer rounds microtones to a semitone with a diagnostic, so a
    // value this large means the file really said so. Hiding it would be worse
    // than showing it.
    CHECK(spelledName(pitchFromSpelling(Step::g, 3, 4)) == "G###4");
}

TEST_CASE("the accidental alone is available for a caller spelling its own name", "[score][pitch]")
{
    CHECK(accidentalName(0).empty());
    CHECK(accidentalName(1) == "#");
    CHECK(accidentalName(-1) == "b");
    CHECK(accidentalName(-2) == "bb");
}

TEST_CASE("every step has a name", "[score][pitch]")
{
    CHECK(stepName(Step::c) == "C");
    CHECK(stepName(Step::d) == "D");
    CHECK(stepName(Step::e) == "E");
    CHECK(stepName(Step::f) == "F");
    CHECK(stepName(Step::g) == "G");
    CHECK(stepName(Step::a) == "A");
    CHECK(stepName(Step::b) == "B");
}
