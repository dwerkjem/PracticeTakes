#include <catch2/catch_test_macros.hpp>

#include "features/score/engraving/StaffPosition.h"

#include <optional>

using namespace engraving;

namespace
{
// The clefs by name, so a test case reads as the clef a musician would say.
const score::Clef treble{"G", 2, 0, 1};
const score::Clef bass{"F", 4, 0, 1};
const score::Clef alto{"C", 3, 0, 1};
const score::Clef tenor{"C", 4, 0, 1};

// The vocal tenor clef: a treble clef that reads an octave above where it
// sounds, which is how a tenor part is written.
const score::Clef vocalTenor{"G", 2, -1, 1};

[[nodiscard]] StaffSteps
positionOf(const score::Clef& clef, score::Step step, int alter, int octave)
{
    const std::optional<StaffSteps> position =
        staffPositionFor(clef, score::pitchFromSpelling(step, alter, octave));

    REQUIRE(position.has_value());

    return *position;
}
} // namespace

TEST_CASE("the five staff lines are where a reader counts them", "[score][engraving][staff]")
{
    // Counting from the bottom line as 1, the way MusicXML names the line a
    // clef sits on. Half spaces downwards from the top line.
    CHECK(stepsForLineFromBottom(5) == topLineSteps);
    CHECK(stepsForLineFromBottom(3) == middleLineSteps);
    CHECK(stepsForLineFromBottom(1) == bottomLineSteps);

    CHECK(isOnLine(topLineSteps));
    CHECK(isOnLine(bottomLineSteps));
    CHECK_FALSE(isOnLine(topLineSteps + 1));
    CHECK(isOnLine(-2));
    CHECK_FALSE(isOnLine(-1));
}

TEST_CASE("a treble staff puts its pitches where they are engraved", "[score][engraving][staff]")
{
    CHECK(positionOf(treble, score::Step::f, 0, 5) == topLineSteps);
    CHECK(positionOf(treble, score::Step::b, 0, 4) == middleLineSteps);
    CHECK(positionOf(treble, score::Step::e, 0, 4) == bottomLineSteps);

    // The G the clef is named for, on the second line from the bottom.
    CHECK(positionOf(treble, score::Step::g, 0, 4) == stepsForLineFromBottom(2));
}

TEST_CASE("a bass staff puts its pitches where they are engraved", "[score][engraving][staff]")
{
    CHECK(positionOf(bass, score::Step::a, 0, 3) == topLineSteps);
    CHECK(positionOf(bass, score::Step::d, 0, 3) == middleLineSteps);
    CHECK(positionOf(bass, score::Step::g, 0, 2) == bottomLineSteps);

    // The F the clef is named for, on the fourth line.
    CHECK(positionOf(bass, score::Step::f, 0, 3) == stepsForLineFromBottom(4));
}

// The one position every musician checks a clef against.
TEST_CASE("middle C lands where each clef puts it", "[score][engraving][staff]")
{
    // One ledger line below the treble staff.
    CHECK(positionOf(treble, score::Step::c, 0, 4) == bottomLineSteps + 2);

    // One ledger line above the bass staff.
    CHECK(positionOf(bass, score::Step::c, 0, 4) == topLineSteps - 2);

    // On the middle line of an alto staff -- which is the whole point of a C
    // clef: the line it sits on is middle C.
    CHECK(positionOf(alto, score::Step::c, 0, 4) == middleLineSteps);

    // And on the fourth line of a tenor staff, because that is where the sign
    // moved to.
    CHECK(positionOf(tenor, score::Step::c, 0, 4) == stepsForLineFromBottom(4));
}

// The enharmonic case the model keeps its spelling for. Pitch.h names this
// issue in its own comments: "C-sharp and D-flat are the same sounding number
// but different staff positions, so a model holding only the number cannot be
// engraved."
TEST_CASE("two spellings of one sounding pitch sit on different lines", "[score][engraving][staff]")
{
    const score::Pitch cSharp = score::pitchFromSpelling(score::Step::c, 1, 5);
    const score::Pitch dFlat = score::pitchFromSpelling(score::Step::d, -1, 5);

    REQUIRE(score::soundsSameAs(cSharp, dFlat));

    const std::optional<StaffSteps> sharpPosition = staffPositionFor(treble, cSharp);
    const std::optional<StaffSteps> flatPosition = staffPositionFor(treble, dFlat);

    REQUIRE(sharpPosition.has_value());
    REQUIRE(flatPosition.has_value());
    CHECK(*sharpPosition != *flatPosition);

    // One diatonic step apart, which is one half space.
    CHECK(*sharpPosition - *flatPosition == 1);
}

// B-sharp sounds as C but is written in the octave below it. A position derived
// from the sounding number would be in the wrong octave as well as on the wrong
// line.
TEST_CASE("a spelling that crosses the octave keeps its own position", "[score][engraving][staff]")
{
    const score::Pitch bSharp = score::pitchFromSpelling(score::Step::b, 1, 3);

    REQUIRE(bSharp.midiNoteNumber == score::pitchFromSpelling(score::Step::c, 0, 4).midiNoteNumber);

    // B3 in treble, not C4: one half space below middle C, which is itself two
    // below the bottom line.
    CHECK(positionOf(treble, score::Step::c, 0, 4) == bottomLineSteps + 2);
    CHECK(positionOf(treble, score::Step::b, 1, 3) == bottomLineSteps + 3);
}

// Undoing the clef's octave change rather than applying it. A tenor part's E3
// is written on the bottom line of a treble staff, exactly where a soprano's E4
// goes; getting the sign backwards would put the whole part two octaves out
// without anything failing.
TEST_CASE("an octave-transposing clef draws where it reads", "[score][engraving][staff]")
{
    CHECK(positionOf(vocalTenor, score::Step::e, 0, 3) == bottomLineSteps);
    CHECK(positionOf(vocalTenor, score::Step::f, 0, 4) == topLineSteps);

    // The same written pitch under a plain treble clef is an octave away.
    CHECK(positionOf(treble, score::Step::e, 0, 3) == bottomLineSteps + 7);
}

TEST_CASE("an alteration does not move a note off its line", "[score][engraving][staff]")
{
    // A sharp changes the sound, never the staff position: the accidental is
    // what carries it.
    const StaffSteps natural = positionOf(treble, score::Step::f, 0, 5);

    CHECK(positionOf(treble, score::Step::f, 1, 5) == natural);
    CHECK(positionOf(treble, score::Step::f, -1, 5) == natural);
    CHECK(positionOf(treble, score::Step::f, 2, 5) == natural);
}

TEST_CASE(
    "ledger lines are counted per line position beyond the staff",
    "[score][engraving][staff]")
{
    // Inside the staff, and the space immediately outside it, need none.
    for (const StaffSteps inside : {-1, 0, 4, 8, 9})
    {
        INFO("position " << inside);
        CHECK(ledgerLinesFor(inside).above == 0);
        CHECK(ledgerLinesFor(inside).below == 0);
    }

    // Middle C below a treble staff: one line.
    CHECK(ledgerLinesFor(bottomLineSteps + 2).below == 1);

    // The space below that line is still one line.
    CHECK(ledgerLinesFor(bottomLineSteps + 3).below == 1);
    CHECK(ledgerLinesFor(bottomLineSteps + 4).below == 2);

    CHECK(ledgerLinesFor(topLineSteps - 2).above == 1);
    CHECK(ledgerLinesFor(topLineSteps - 3).above == 1);
    CHECK(ledgerLinesFor(topLineSteps - 4).above == 2);
}

TEST_CASE("a single voice stems up below the middle line", "[score][engraving][staff]")
{
    CHECK(stemPointsUpFor(bottomLineSteps));
    CHECK(stemPointsUpFor(middleLineSteps + 1));

    // On the middle line and above, the stem goes down.
    CHECK_FALSE(stemPointsUpFor(middleLineSteps));
    CHECK_FALSE(stemPointsUpFor(topLineSteps));
}

// A percussion or tablature staff names no pitch, so there is no position to
// return. Returning one anyway -- the middle line, say -- would put every note
// of a drum part in the same wrong place and look deliberate.
TEST_CASE("a clef that names no pitch places nothing", "[score][engraving][staff]")
{
    for (const char* sign : {"percussion", "TAB", "none", "unknown"})
    {
        const score::Clef unpitched{sign, 2, 0, 1};

        INFO("clef sign " << sign);
        CHECK_FALSE(isPitchedClef(unpitched));
        CHECK_FALSE(staffPositionFor(unpitched, score::pitchFromSpelling(score::Step::c, 0, 4))
                        .has_value());
    }

    CHECK(isPitchedClef(treble));
    CHECK(isPitchedClef(bass));
    CHECK(isPitchedClef(alto));
}
