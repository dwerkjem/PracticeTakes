#include <catch2/catch_test_macros.hpp>

#include "features/score/engraving/MeasureAccidentals.h"

using namespace engraving;
using score::Step;

namespace
{
const score::KeySignature cMajor{0, "major"};
const score::KeySignature dMajor{2, "major"};
const score::KeySignature fMajor{-1, "major"};

[[nodiscard]] score::Pitch note(Step step, int alter, int octave)
{
    return score::pitchFromSpelling(step, alter, octave);
}
} // namespace

TEST_CASE("the key signature decides what is already in force", "[score][engraving][accidental]")
{
    // Two sharps: F and C, in the order the circle of fifths alters them.
    CHECK(keyAlterationFor(dMajor, Step::f) == 1);
    CHECK(keyAlterationFor(dMajor, Step::c) == 1);
    CHECK(keyAlterationFor(dMajor, Step::g) == 0);

    CHECK(keyAlterationFor(fMajor, Step::b) == -1);
    CHECK(keyAlterationFor(fMajor, Step::e) == 0);

    for (const Step step : {Step::c, Step::d, Step::e, Step::f, Step::g, Step::a, Step::b})
    {
        CHECK(keyAlterationFor(cMajor, step) == 0);
    }
}

TEST_CASE("a note inside the key draws no accidental", "[score][engraving][accidental]")
{
    MeasureAccidentals accidentals(dMajor);

    // F-sharp in D major is what the signature already says.
    CHECK(accidentals.accidentalFor(note(Step::f, 1, 4), false) == AccidentalKind::none);
    CHECK(accidentals.accidentalFor(note(Step::c, 1, 5), false) == AccidentalKind::none);

    // And a note the signature says nothing about is drawn plain.
    CHECK(accidentals.accidentalFor(note(Step::g, 0, 4), false) == AccidentalKind::none);
}

TEST_CASE("a note outside the key draws its accidental", "[score][engraving][accidental]")
{
    MeasureAccidentals accidentals(cMajor);

    CHECK(accidentals.accidentalFor(note(Step::f, 1, 4), false) == AccidentalKind::sharp);
    CHECK(accidentals.accidentalFor(note(Step::b, -1, 4), false) == AccidentalKind::flat);
    CHECK(accidentals.accidentalFor(note(Step::g, 2, 4), false) == AccidentalKind::doubleSharp);
    CHECK(accidentals.accidentalFor(note(Step::a, -2, 4), false) == AccidentalKind::doubleFlat);
}

TEST_CASE("an accidental is not repeated within the bar", "[score][engraving][accidental]")
{
    MeasureAccidentals accidentals(cMajor);

    REQUIRE(accidentals.accidentalFor(note(Step::f, 1, 4), false) == AccidentalKind::sharp);

    // The same F, later in the same bar: the sharp is still in force.
    CHECK(accidentals.accidentalFor(note(Step::f, 1, 4), false) == AccidentalKind::none);
    CHECK(accidentals.alterationInForce(Step::f, 4) == 1);
}

// Modern notation keys an accidental to one octave, which is also what MusicXML
// assumes. An F-sharp in one octave says nothing about the F an octave up.
TEST_CASE("an accidental applies to its own octave only", "[score][engraving][accidental]")
{
    MeasureAccidentals accidentals(cMajor);

    REQUIRE(accidentals.accidentalFor(note(Step::f, 1, 4), false) == AccidentalKind::sharp);

    CHECK(accidentals.accidentalFor(note(Step::f, 1, 5), false) == AccidentalKind::sharp);
    CHECK(accidentals.alterationInForce(Step::f, 3) == 0);
}

// The key signature, by contrast, applies in every octave -- which is the whole
// difference between a signature and an accidental.
TEST_CASE("the key signature applies in every octave", "[score][engraving][accidental]")
{
    MeasureAccidentals accidentals(dMajor);

    for (const int octave : {2, 4, 6})
    {
        INFO("octave " << octave);
        CHECK(accidentals.accidentalFor(note(Step::f, 1, octave), false) == AccidentalKind::none);
        CHECK(accidentals.alterationInForce(Step::f, octave) == 1);
    }
}

TEST_CASE("cancelling an alteration draws a natural", "[score][engraving][accidental]")
{
    SECTION("one the key signature made")
    {
        MeasureAccidentals accidentals(dMajor);

        CHECK(accidentals.accidentalFor(note(Step::f, 0, 4), false) == AccidentalKind::natural);
    }

    SECTION("one an earlier note in the bar made")
    {
        MeasureAccidentals accidentals(cMajor);

        REQUIRE(accidentals.accidentalFor(note(Step::g, 1, 4), false) == AccidentalKind::sharp);
        CHECK(accidentals.accidentalFor(note(Step::g, 0, 4), false) == AccidentalKind::natural);

        // And the natural is now what is in force, so a third G draws nothing.
        CHECK(accidentals.accidentalFor(note(Step::g, 0, 4), false) == AccidentalKind::none);
    }
}

TEST_CASE(
    "a change of alteration within the bar draws the new sign",
    "[score][engraving][accidental]")
{
    MeasureAccidentals accidentals(cMajor);

    REQUIRE(accidentals.accidentalFor(note(Step::e, -1, 4), false) == AccidentalKind::flat);
    CHECK(accidentals.accidentalFor(note(Step::e, 1, 4), false) == AccidentalKind::sharp);
    CHECK(accidentals.alterationInForce(Step::e, 4) == 1);
}

// Reset at each barline, which is the rule stated as a lifetime: a new measure
// is a new object, so nobody has to remember to call anything.
TEST_CASE("a new measure forgets the previous bar's accidentals", "[score][engraving][accidental]")
{
    MeasureAccidentals first(dMajor);

    REQUIRE(first.accidentalFor(note(Step::g, 1, 4), false) == AccidentalKind::sharp);
    REQUIRE(first.alterationInForce(Step::g, 4) == 1);

    const MeasureAccidentals second(dMajor);

    CHECK(second.alterationInForce(Step::g, 4) == 0);

    // While the signature's own alterations apply again.
    CHECK(second.alterationInForce(Step::f, 4) == 1);
}

// The accidental belongs to the note that was struck, not to the one holding it
// on. This is the common case of a tie across a barline, where repeating the
// sign would be wrong.
TEST_CASE("a note held from a tie draws no accidental", "[score][engraving][accidental]")
{
    MeasureAccidentals accidentals(cMajor);

    CHECK(accidentals.accidentalFor(note(Step::f, 1, 4), true) == AccidentalKind::none);

    // And it leaves what is in force alone, because the pitch did not change:
    // a *struck* F-sharp later in the bar still needs its sign.
    CHECK(accidentals.alterationInForce(Step::f, 4) == 0);
    CHECK(accidentals.accidentalFor(note(Step::f, 1, 4), false) == AccidentalKind::sharp);
}

TEST_CASE(
    "an alteration beyond a double is shown rather than dropped",
    "[score][engraving][accidental]")
{
    // The importer already rounds microtones to a semitone with a diagnostic,
    // so a value this large means the file said so. score::accidentalName takes
    // the same view one layer down.
    CHECK(accidentalKindFor(3) == AccidentalKind::doubleSharp);
    CHECK(accidentalKindFor(-3) == AccidentalKind::doubleFlat);
    CHECK(accidentalKindFor(0) == AccidentalKind::natural);
}
