#include <catch2/catch_test_macros.hpp>

#include "platform/pitch/TargetChoice.h"
#include "platform/score/PitchNames.h"

#include <string>

using namespace pitch;

namespace
{
[[nodiscard]] std::string nameOf(const std::optional<score::Pitch>& pitch)
{
    return pitch.has_value() ? score::spelledName(*pitch) : std::string("none");
}
} // namespace

TEST_CASE("a note name is read however it is typed", "[pitch][choice]")
{
    CHECK(nameOf(parseNoteName("F#4")) == "F#4");
    CHECK(nameOf(parseNoteName("Bb3")) == "Bb3");
    CHECK(nameOf(parseNoteName(" c 4 ")) == "C4");
    CHECK(
        nameOf(parseNoteName("E\xE2\x99\xAD"
                             "5")) == "Eb5");
    CHECK(
        nameOf(parseNoteName("G\xE2\x99\xAF"
                             "2")) == "G#2");
    CHECK(nameOf(parseNoteName("C-1")) == "C-1");
    CHECK(nameOf(parseNoteName("A##4")) == "A##4");
    CHECK(parseNoteName("A4").value().midiNoteNumber == 69);

    CHECK_FALSE(parseNoteName("").has_value());
    CHECK_FALSE(parseNoteName("H4").has_value());
    CHECK_FALSE(parseNoteName("C").has_value());
    CHECK_FALSE(parseNoteName("C10").has_value());
    CHECK_FALSE(parseNoteName("G9#").has_value());
    CHECK_FALSE(parseNoteName("Cbbb4").has_value());
    CHECK_FALSE(parseNoteName("G#9").has_value());
}

TEST_CASE("a staff position is a pitch, with its accidental", "[pitch][choice]")
{
    CHECK(nameOf(pitchAtStaffPosition(StaffClef::treble, 0, 0)) == "B4");
    CHECK(nameOf(pitchAtStaffPosition(StaffClef::treble, -6, 0)) == "C4");
    CHECK(nameOf(pitchAtStaffPosition(StaffClef::treble, 4, 1)) == "F#5");
    CHECK(nameOf(pitchAtStaffPosition(StaffClef::bass, 0, 0)) == "D3");
    CHECK(nameOf(pitchAtStaffPosition(StaffClef::bass, 6, 0)) == "C4");
    CHECK(nameOf(pitchAtStaffPosition(StaffClef::bass, -4, -1)) == "Gb2");
}

TEST_CASE("a semitone up is spelled sharp, and down flat", "[pitch][choice]")
{
    const score::Pitch c4 = score::pitchFromSpelling(score::Step::c, 0, 4);
    CHECK(nameOf(stepBySemitone(c4, 1)) == "C#4");
    CHECK(nameOf(stepBySemitone(c4, -1)) == "B3");
    CHECK(nameOf(stepBySemitone(score::pitchFromSpelling(score::Step::e, 0, 4), -1)) == "Eb4");
    CHECK_FALSE(stepBySemitone(score::pitchFromSpelling(score::Step::g, 0, 9), 1).has_value());
}

TEST_CASE("a step of the key keeps the key's spelling", "[pitch][choice]")
{
    // D major (two sharps): E4 up is F#4, C#5 up is D5; B3 down in F major is A3.
    const score::Pitch e4 = score::pitchFromSpelling(score::Step::e, 0, 4);
    CHECK(nameOf(stepInKey(e4, 1, 2)) == "F#4");
    CHECK(nameOf(stepInKey(score::pitchFromSpelling(score::Step::c, 1, 5), 1, 2)) == "D5");
    CHECK(nameOf(stepInKey(score::pitchFromSpelling(score::Step::c, 0, 4), -1, -1)) == "Bb3");
    CHECK(nameOf(stepInKey(score::pitchFromSpelling(score::Step::b, 0, 3), 1, 0)) == "C4");

    CHECK(keyAlteration(score::Step::f, 1) == 1);
    CHECK(keyAlteration(score::Step::c, 1) == 0);
    CHECK(keyAlteration(score::Step::b, -1) == -1);
    CHECK(keyAlteration(score::Step::e, -1) == 0);
    CHECK(keyAlteration(score::Step::f, 7) == 1);
    CHECK(keyAlteration(score::Step::f, -7) == -1);
}

TEST_CASE("a key is spelled with sharps, or flats when asked", "[pitch][choice]")
{
    CHECK(nameOf(spellMidi(61, true)) == "C#4");
    CHECK(nameOf(spellMidi(61, false)) == "Db4");
    CHECK(nameOf(spellMidi(60, true)) == "C4");
    CHECK(nameOf(spellMidi(0, true)) == "C-1");
    CHECK_FALSE(spellMidi(128, true).has_value());
}

TEST_CASE("a pitch's place on a staff, in each clef", "[pitch][choice]")
{
    const score::Pitch middleC = score::pitchFromSpelling(score::Step::c, 0, 4);
    CHECK(staffStepsOf(middleC, StaffClef::treble) == -6);
    CHECK(staffStepsOf(middleC, StaffClef::bass) == 6);
    CHECK(staffStepsOf(middleC, StaffClef::trebleOctaveDown) == 1);
    CHECK(staffStepsOf(score::pitchFromSpelling(score::Step::f, 1, 5), StaffClef::treble) == 4);

    // The two directions agree.
    for (const StaffClef clef : {StaffClef::treble, StaffClef::bass, StaffClef::trebleOctaveDown})
    {
        for (int steps = -8; steps <= 8; ++steps)
        {
            const auto pitch = pitchAtStaffPosition(clef, steps, 0);
            REQUIRE(pitch.has_value());
            CHECK(staffStepsOf(*pitch, clef) == steps);
        }
    }
}

TEST_CASE("a key signature names its major and relative minor", "[pitch][key]")
{
    using score::Step;
    const auto c = pitch::keyTonics(0);
    CHECK(c.majorStep == Step::c);
    CHECK(c.majorAlter == 0);
    CHECK(c.minorStep == Step::a);

    const auto g = pitch::keyTonics(1);
    CHECK(g.majorStep == Step::g);
    CHECK(g.minorStep == Step::e);

    const auto bFlat = pitch::keyTonics(-2);
    CHECK(bFlat.majorStep == Step::b);
    CHECK(bFlat.majorAlter == -1);
    CHECK(bFlat.minorStep == Step::g);
    CHECK(bFlat.minorAlter == 0);

    const auto cSharp = pitch::keyTonics(7);
    CHECK(cSharp.majorStep == Step::c);
    CHECK(cSharp.majorAlter == 1);
    CHECK(cSharp.minorStep == Step::a);
    CHECK(cSharp.minorAlter == 1);

    const auto cFlat = pitch::keyTonics(-7);
    CHECK(cFlat.majorStep == Step::c);
    CHECK(cFlat.majorAlter == -1);
    CHECK(cFlat.minorStep == Step::a);
    CHECK(cFlat.minorAlter == -1);
}

TEST_CASE("a heard note takes the key's accidentals, and the reader's in C", "[pitch][key]")
{
    // A flat key spells with flats and a sharp one with sharps, whatever the
    // reader prefers; C major, which has neither, takes the preference.
    CHECK(pitch::spellForKey(61, -3, false)->alter == -1);
    CHECK(pitch::spellForKey(61, 2, true)->alter == 1);
    CHECK(pitch::spellForKey(61, 0, false)->alter == 1);
    CHECK(pitch::spellForKey(61, 0, true)->alter == -1);
    CHECK(pitch::spellForKey(61, 0)->alter == 1);
}

TEST_CASE("a pitch is in a key when it is one of its seven notes", "[pitch][key]")
{
    // C major: the white keys.
    CHECK(pitch::inKey(60, 0));
    CHECK_FALSE(pitch::inKey(61, 0));
    CHECK(pitch::inKey(71, 0));
    // G major has F sharp, not F.
    CHECK(pitch::inKey(66, 1));
    CHECK_FALSE(pitch::inKey(65, 1));
    // E flat major (and C minor) has A flat, not A.
    CHECK(pitch::inKey(68, -3));
    CHECK_FALSE(pitch::inKey(69, -3));
    // Low and negative numbers work alike.
    CHECK(pitch::inKey(0, 0));
    CHECK_FALSE(pitch::inKey(-11, 0));
}
