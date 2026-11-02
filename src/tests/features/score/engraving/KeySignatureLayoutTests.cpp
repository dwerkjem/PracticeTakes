#include <catch2/catch_test_macros.hpp>

#include "features/score/engraving/KeySignatureLayout.h"

#include <vector>

using namespace engraving;

namespace
{
const score::Clef treble{"G", 2, 0, 1};
const score::Clef bass{"F", 4, 0, 1};
const score::Clef alto{"C", 3, 0, 1};

[[nodiscard]] std::vector<StaffSteps> positionsFor(const score::Clef& clef, int fifths)
{
    std::vector<StaffSteps> positions;

    for (const KeySignatureAccidental& accidental :
         keySignatureAccidentals(clef, score::KeySignature{fifths, ""}))
    {
        positions.push_back(accidental.position);
    }

    return positions;
}

[[nodiscard]] std::vector<score::Step> stepsFor(const score::Clef& clef, int fifths)
{
    std::vector<score::Step> steps;

    for (const KeySignatureAccidental& accidental :
         keySignatureAccidentals(clef, score::KeySignature{fifths, ""}))
    {
        steps.push_back(accidental.step);
    }

    return steps;
}
} // namespace

TEST_CASE("a key of no accidentals draws nothing", "[score][engraving][key]")
{
    // Not seven naturals. C major draws an empty key signature, and a renderer
    // that drew cancellations instead would be wrong rather than thorough.
    CHECK(keySignatureAccidentals(treble, score::KeySignature{0, "major"}).empty());
    CHECK(keySignatureAccidentalCount(score::KeySignature{0, ""}) == 0);
}

TEST_CASE("the accidentals follow the circle of fifths", "[score][engraving][key]")
{
    using score::Step;

    CHECK(
        stepsFor(treble, 7) ==
        std::vector<Step>{Step::f, Step::c, Step::g, Step::d, Step::a, Step::e, Step::b});

    CHECK(
        stepsFor(treble, -7) ==
        std::vector<Step>{Step::b, Step::e, Step::a, Step::d, Step::g, Step::c, Step::f});

    // A key of three sharps is the first three of the seven, in that order.
    CHECK(stepsFor(treble, 3) == std::vector<Step>{Step::f, Step::c, Step::g});
}

// The positions a reader checks a key signature against. Treble F-sharp is on
// the top line; treble B-flat is on the middle line. These are the engraved
// convention, and they are what the hand-checked table in KeySignatureLayout
// exists to reproduce.
TEST_CASE("treble positions match the engraved convention", "[score][engraving][key]")
{
    CHECK(positionsFor(treble, 7) == std::vector<StaffSteps>{0, 3, -1, 2, 5, 1, 4});
    CHECK(positionsFor(treble, -7) == std::vector<StaffSteps>{4, 1, 5, 2, 6, 3, 7});

    // The first sharp is the top line, F5.
    CHECK(positionsFor(treble, 1) == std::vector<StaffSteps>{topLineSteps});

    // The first flat is the middle line, B4.
    CHECK(positionsFor(treble, -1) == std::vector<StaffSteps>{middleLineSteps});
}

TEST_CASE("bass positions match the engraved convention", "[score][engraving][key]")
{
    CHECK(positionsFor(bass, 7) == std::vector<StaffSteps>{2, 5, 1, 4, 7, 3, 6});
    CHECK(positionsFor(bass, -7) == std::vector<StaffSteps>{6, 3, 7, 4, 8, 5, 9});

    // The first sharp is the fourth line, F3 -- the line the clef is named for.
    CHECK(positionsFor(bass, 1) == std::vector<StaffSteps>{stepsForLineFromBottom(4)});

    // The first flat is the second line, B2.
    CHECK(positionsFor(bass, -1) == std::vector<StaffSteps>{stepsForLineFromBottom(2)});
}

TEST_CASE("alto positions match the engraved convention", "[score][engraving][key]")
{
    CHECK(positionsFor(alto, 7) == std::vector<StaffSteps>{1, 4, 0, 3, 6, 2, 5});
    CHECK(positionsFor(alto, -7) == std::vector<StaffSteps>{5, 2, 6, 3, 7, 4, 8});
}

// Each accidental is a sharp or a flat, never a mixture, and never a natural.
TEST_CASE("a signature is all sharps or all flats", "[score][engraving][key]")
{
    for (const KeySignatureAccidental& accidental :
         keySignatureAccidentals(treble, score::KeySignature{5, ""}))
    {
        CHECK(accidental.kind == AccidentalKind::sharp);
    }

    for (const KeySignatureAccidental& accidental :
         keySignatureAccidentals(treble, score::KeySignature{-5, ""}))
    {
        CHECK(accidental.kind == AccidentalKind::flat);
    }
}

TEST_CASE("a file declaring more than seven is clamped", "[score][engraving][key]")
{
    // Malformed input, not a programming error: the notes are still worth
    // drawing, so the signature is clamped rather than the file refused.
    CHECK(keySignatureAccidentalCount(score::KeySignature{11, ""}) == 7);
    CHECK(keySignatureAccidentalCount(score::KeySignature{-11, ""}) == 7);
    CHECK(keySignatureAccidentals(treble, score::KeySignature{11, ""}).size() == 7);
}

TEST_CASE("an unpitched clef draws no key signature", "[score][engraving][key]")
{
    const score::Clef percussion{"percussion", 3, 0, 1};

    CHECK(keySignatureAccidentals(percussion, score::KeySignature{3, ""}).empty());
    CHECK_FALSE(keySignatureIsConventional(percussion));
}

// A clef of a known sign on an unusual line keeps the *pitches* the key names
// and shifts with the staff. That is derived rather than checked against an
// engraved reference, which is why the layout says so in a notice.
TEST_CASE("an unusual clef line shifts the whole signature", "[score][engraving][key]")
{
    CHECK(keySignatureIsConventional(treble));
    CHECK(keySignatureIsConventional(bass));
    CHECK(keySignatureIsConventional(alto));

    const score::Clef frenchViolin{"G", 1, 0, 1};

    CHECK_FALSE(keySignatureIsConventional(frenchViolin));

    // One line lower is two half spaces further down the staff, for every
    // accidental at once.
    const std::vector<StaffSteps> shifted = positionsFor(frenchViolin, 4);
    const std::vector<StaffSteps> conventional = positionsFor(treble, 4);

    REQUIRE(shifted.size() == conventional.size());

    for (std::size_t index = 0; index < shifted.size(); ++index)
    {
        CHECK(shifted.at(index) == conventional.at(index) + 2);
    }
}

TEST_CASE("an octave-transposing clef signs the same positions", "[score][engraving][key]")
{
    // A vocal tenor clef reads as a treble clef, so its key signature is drawn
    // in the same places. The octave change is about sounding pitch.
    const score::Clef vocalTenor{"G", 2, -1, 1};

    CHECK(positionsFor(vocalTenor, 3) == positionsFor(treble, 3));
    CHECK(keySignatureIsConventional(vocalTenor));
}
