#include <catch2/catch_test_macros.hpp>

#include "platform/score/engraving/StaffPosition.h"

using namespace score;
using namespace score::engraving;

namespace
{
[[nodiscard]] Clef trebleClef()
{
    return Clef{"G", 2, 0, 1};
}

[[nodiscard]] Clef bassClef()
{
    return Clef{"F", 4, 0, 1};
}

[[nodiscard]] Clef altoClef()
{
    return Clef{"C", 3, 0, 1};
}
} // namespace

TEST_CASE("a clef puts its own pitch on its own line", "[engraving][staff]")
{
    // The definition of a clef, stated as the test that would catch getting it
    // backwards: line 2 is step 2, line 4 is step 6, line 3 is step 4.
    CHECK(staffStepFor(pitchFromSpelling(Step::g, 0, 4), trebleClef()) == 2);
    CHECK(staffStepFor(pitchFromSpelling(Step::f, 0, 3), bassClef()) == 6);
    CHECK(staffStepFor(pitchFromSpelling(Step::c, 0, 4), altoClef()) == 4);
}

TEST_CASE("the staff's own lines land where they should", "[engraving][staff]")
{
    // Treble staff, bottom to top: E4 G4 B4 D5 F5.
    CHECK(staffStepFor(pitchFromSpelling(Step::e, 0, 4), trebleClef()) == 0);
    CHECK(staffStepFor(pitchFromSpelling(Step::b, 0, 4), trebleClef()) == 4);
    CHECK(staffStepFor(pitchFromSpelling(Step::f, 0, 5), trebleClef()) == 8);

    // Bass staff, bottom to top: G2 B2 D3 F3 A3.
    CHECK(staffStepFor(pitchFromSpelling(Step::g, 0, 2), bassClef()) == 0);
    CHECK(staffStepFor(pitchFromSpelling(Step::d, 0, 3), bassClef()) == 4);
    CHECK(staffStepFor(pitchFromSpelling(Step::a, 0, 3), bassClef()) == 8);
}

TEST_CASE("middle C sits one ledger line outside both staves", "[engraving][staff]")
{
    // The one position every musician can check by eye, and the one that proves
    // the two clefs are a tenth apart rather than an octave.
    CHECK(staffStepFor(pitchFromSpelling(Step::c, 0, 4), trebleClef()) == -2);
    CHECK(staffStepFor(pitchFromSpelling(Step::c, 0, 4), bassClef()) == 10);
}

TEST_CASE("an accidental does not move a notehead", "[engraving][staff]")
{
    // F sharp and F flat are the same line as F natural. That is the whole
    // reason the model keeps a spelling rather than a MIDI number, so it gets
    // its own case.
    const int natural = staffStepFor(pitchFromSpelling(Step::f, 0, 5), trebleClef());

    CHECK(staffStepFor(pitchFromSpelling(Step::f, 1, 5), trebleClef()) == natural);
    CHECK(staffStepFor(pitchFromSpelling(Step::f, -1, 5), trebleClef()) == natural);
    CHECK(staffStepFor(pitchFromSpelling(Step::f, 2, 5), trebleClef()) == natural);
}

TEST_CASE("enharmonics that differ in spelling differ on the staff", "[engraving][staff]")
{
    // C sharp 5 and D flat 5 sound the same and are written a step apart. A
    // renderer driven by the sounding number could not tell them apart at all.
    const Pitch cSharp = pitchFromSpelling(Step::c, 1, 5);
    const Pitch dFlat = pitchFromSpelling(Step::d, -1, 5);

    REQUIRE(cSharp.midiNoteNumber == dFlat.midiNoteNumber);
    CHECK(staffStepFor(cSharp, trebleClef()) != staffStepFor(dFlat, trebleClef()));
}

TEST_CASE("lines and spaces alternate", "[engraving][staff]")
{
    CHECK(isOnLine(0));
    CHECK_FALSE(isOnLine(1));
    CHECK(isOnLine(8));
    CHECK(isOnLine(-2));
}

TEST_CASE("a note inside the staff needs no ledger lines", "[engraving][staff][ledger]")
{
    for (int step = bottomLineStep; step <= topLineStep; ++step)
    {
        INFO("step " << step);
        CHECK(ledgerLinesFor(step).empty());
    }
}

TEST_CASE("ledger lines reach from the staff to the note", "[engraving][staff][ledger]")
{
    // Middle C in treble is step -2: one ledger line, at -2 itself.
    const LedgerLineRange middleC = ledgerLinesFor(-2);

    REQUIRE(middleC.count() == 1);
    CHECK(middleC.beginStep == -2);
    CHECK(middleC.stride == -2);

    // A5 is step 12: two ledger lines above, at 10 and 12.
    const LedgerLineRange high = ledgerLinesFor(12);

    REQUIRE(high.count() == 2);
    CHECK(high.beginStep == 10);
    CHECK(high.stride == 2);
}

TEST_CASE(
    "a note in a space outside the staff hangs off the last line",
    "[engraving][staff][ledger]")
{
    // Step 11 is a space. The outermost ledger line is the one below it, at 10,
    // not one drawn through the note itself.
    const LedgerLineRange above = ledgerLinesFor(11);

    REQUIRE(above.count() == 1);
    CHECK(above.beginStep == 10);

    // Step -3 is a space below; its one ledger line is at -2.
    const LedgerLineRange below = ledgerLinesFor(-3);

    REQUIRE(below.count() == 1);
    CHECK(below.beginStep == -2);
}

TEST_CASE("an unpitched clef sign still places notes", "[engraving][staff]")
{
    // "percussion", "TAB" and "none" are kept verbatim by the model. Falling
    // back to treble places them somewhere rather than refusing; what matters
    // here is that it does not crash or return something wild.
    const Clef percussion{"percussion", 3, 0, 1};

    CHECK(
        staffStepFor(pitchFromSpelling(Step::g, 0, 4), percussion) ==
        staffStepFor(pitchFromSpelling(Step::g, 0, 4), Clef{"G", 3, 0, 1}));
}
