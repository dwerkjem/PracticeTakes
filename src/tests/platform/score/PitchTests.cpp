#include <catch2/catch_test_macros.hpp>

#include "platform/score/Pitch.h"

using namespace score;

TEST_CASE("middle C is C4 and MIDI 60", "[score][pitch]")
{
    const Pitch middleC = pitchFromSpelling(Step::c, 0, 4);

    CHECK(middleC.midiNoteNumber == 60);
    CHECK(isConsistent(middleC));
}

TEST_CASE("the natural steps sit at the expected semitones", "[score][pitch]")
{
    CHECK(midiNoteNumberFor(Step::c, 0, 4) == 60);
    CHECK(midiNoteNumberFor(Step::d, 0, 4) == 62);
    CHECK(midiNoteNumberFor(Step::e, 0, 4) == 64);
    CHECK(midiNoteNumberFor(Step::f, 0, 4) == 65);
    CHECK(midiNoteNumberFor(Step::g, 0, 4) == 67);
    CHECK(midiNoteNumberFor(Step::a, 0, 4) == 69);
    CHECK(midiNoteNumberFor(Step::b, 0, 4) == 71);
}

TEST_CASE("A440 is A4 and MIDI 69", "[score][pitch]")
{
    // The one pitch the rest of this application already agrees on, via the
    // tuner's reference frequency.
    CHECK(pitchFromSpelling(Step::a, 0, 4).midiNoteNumber == 69);
}

TEST_CASE("octaves are twelve semitones apart", "[score][pitch]")
{
    for (int octave = 0; octave < 8; ++octave)
    {
        INFO("octave " << octave);
        CHECK(
            midiNoteNumberFor(Step::c, 0, octave + 1) ==
            midiNoteNumberFor(Step::c, 0, octave) + 12);
    }
}

TEST_CASE("enharmonic spellings sound the same but stay distinguishable", "[score][pitch]")
{
    // This is the entire reason the model keeps the spelling: the renderer must
    // be able to tell these apart to pick a staff line and an accidental, and
    // nothing derivable from MIDI 61 can do that.
    const Pitch cSharp = pitchFromSpelling(Step::c, 1, 4);
    const Pitch dFlat = pitchFromSpelling(Step::d, -1, 4);

    CHECK(cSharp.midiNoteNumber == 61);
    CHECK(dFlat.midiNoteNumber == 61);

    CHECK(soundsSameAs(cSharp, dFlat));
    CHECK(cSharp != dFlat);
}

TEST_CASE("double accidentals are spelled and sounded correctly", "[score][pitch]")
{
    const Pitch fDoubleSharp = pitchFromSpelling(Step::f, 2, 4);
    const Pitch g = pitchFromSpelling(Step::g, 0, 4);

    CHECK(fDoubleSharp.midiNoteNumber == 67);
    CHECK(soundsSameAs(fDoubleSharp, g));
    CHECK(fDoubleSharp != g);

    const Pitch bDoubleFlat = pitchFromSpelling(Step::b, -2, 3);

    CHECK(bDoubleFlat.midiNoteNumber == 57);
    CHECK(soundsSameAs(bDoubleFlat, pitchFromSpelling(Step::a, 0, 3)));
}

TEST_CASE("spellings that cross an octave boundary still sound right", "[score][pitch]")
{
    // B-sharp 3 sounds as C4, and C-flat 4 sounds as B3. Both are written in
    // real scores and both are easy to get wrong.
    CHECK(pitchFromSpelling(Step::b, 1, 3).midiNoteNumber == 60);
    CHECK(pitchFromSpelling(Step::c, -1, 4).midiNoteNumber == 59);
}

TEST_CASE("invariant 5 rejects a pitch whose number contradicts its spelling", "[score][pitch]")
{
    // Only reachable by aggregate-initialising a Pitch instead of using
    // pitchFromSpelling, which is exactly the mistake worth catching: such a
    // note renders on one line and sounds as another.
    Pitch inconsistent{Step::c, 0, 4, 61};

    CHECK_FALSE(isConsistent(inconsistent));

    inconsistent.midiNoteNumber = 60;

    CHECK(isConsistent(inconsistent));
}

TEST_CASE("every spelling the MVP subset admits is self-consistent", "[score][pitch]")
{
    for (const Step step : {Step::c, Step::d, Step::e, Step::f, Step::g, Step::a, Step::b})
    {
        for (int alter = -2; alter <= 2; ++alter)
        {
            for (int octave = 0; octave <= 9; ++octave)
            {
                INFO("alter " << alter << " octave " << octave);
                CHECK(isConsistent(pitchFromSpelling(step, alter, octave)));
            }
        }
    }
}
