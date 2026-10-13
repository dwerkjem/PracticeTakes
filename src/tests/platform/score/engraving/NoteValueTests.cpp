#include <catch2/catch_test_macros.hpp>

#include "platform/score/engraving/NoteValue.h"

using namespace score;
using namespace score::engraving;

namespace
{
constexpr Tick quarter = ticksPerQuarterNote;
constexpr Tick half = 2 * quarter;
constexpr Tick whole = 4 * quarter;
constexpr Tick eighth = quarter / 2;
constexpr Tick sixteenth = quarter / 4;
} // namespace

TEST_CASE("plain values decompose to their own notehead and flag count", "[engraving][value]")
{
    // The whole table, because an off-by-one in the flag count is the kind of
    // mistake that draws every sixteenth as an eighth and looks deliberate.
    CHECK(noteValueFor(whole).flags == 0);
    CHECK(noteValueFor(whole).notehead == Glyph::noteheadWhole);
    CHECK(noteValueFor(whole).rest == Glyph::restWhole);

    CHECK(noteValueFor(half).flags == 0);
    CHECK(noteValueFor(half).notehead == Glyph::noteheadHalf);
    CHECK(noteValueFor(half).rest == Glyph::restHalf);

    CHECK(noteValueFor(quarter).flags == 0);
    CHECK(noteValueFor(quarter).notehead == Glyph::noteheadBlack);
    CHECK(noteValueFor(quarter).rest == Glyph::restQuarter);

    CHECK(noteValueFor(eighth).flags == 1);
    CHECK(noteValueFor(eighth).notehead == Glyph::noteheadBlack);
    CHECK(noteValueFor(eighth).rest == Glyph::rest8th);

    CHECK(noteValueFor(sixteenth).flags == 2);
    CHECK(noteValueFor(sixteenth).rest == Glyph::rest16th);

    CHECK(noteValueFor(sixteenth / 2).flags == 3);
    CHECK(noteValueFor(sixteenth / 4).flags == 4);

    // None of them is dotted, and every one is exactly expressible.
    for (const Tick duration :
         {whole, half, quarter, eighth, sixteenth, sixteenth / 2, sixteenth / 4})
    {
        INFO("duration " << duration);
        CHECK(noteValueFor(duration).dots == 0);
        CHECK(noteValueFor(duration).exact);
    }
}

TEST_CASE("dots are counted, not approximated", "[engraving][value]")
{
    const NoteValue dottedHalf = noteValueFor(half + quarter);

    CHECK(dottedHalf.notehead == Glyph::noteheadHalf);
    CHECK(dottedHalf.flags == 0);
    CHECK(dottedHalf.dots == 1);
    CHECK(dottedHalf.exact);

    const NoteValue doubleDottedHalf = noteValueFor(half + quarter + eighth);

    CHECK(doubleDottedHalf.notehead == Glyph::noteheadHalf);
    CHECK(doubleDottedHalf.dots == 2);
    CHECK(doubleDottedHalf.exact);

    const NoteValue tripleDottedHalf = noteValueFor(half + quarter + eighth + sixteenth);

    CHECK(tripleDottedHalf.dots == 3);
    CHECK(tripleDottedHalf.exact);

    const NoteValue dottedEighth = noteValueFor(eighth + sixteenth);

    CHECK(dottedEighth.flags == 1);
    CHECK(dottedEighth.dots == 1);
    CHECK(dottedEighth.exact);
}

TEST_CASE("a tuplet is written as the value it stands in for", "[engraving][value]")
{
    // The whole reason the ratio is a parameter. A triplet eighth lasts two
    // thirds of an eighth; drawing what it lasts rather than what it is written
    // as turns every triplet in every score into a dotted something.
    const TupletRatio threeInTwo{3, 2};

    const NoteValue tripletEighth = noteValueFor((eighth * 2) / 3, threeInTwo);

    CHECK(tripletEighth.flags == 1);
    CHECK(tripletEighth.dots == 0);
    CHECK(tripletEighth.exact);

    const NoteValue tripletQuarter = noteValueFor((quarter * 2) / 3, threeInTwo);

    CHECK(tripletQuarter.flags == 0);
    CHECK(tripletQuarter.notehead == Glyph::noteheadBlack);
    CHECK(tripletQuarter.dots == 0);
    CHECK(tripletQuarter.exact);

    // Five in the time of four, which is the other common one.
    const NoteValue quintupletSixteenth = noteValueFor((sixteenth * 4) / 5, TupletRatio{5, 4});

    CHECK(quintupletSixteenth.flags == 2);
    CHECK(quintupletSixteenth.exact);

    // And the same duration with no ratio reads as something else entirely,
    // which is the point of passing one.
    CHECK_FALSE(noteValueFor((eighth * 2) / 3).exact);
}

TEST_CASE("a duration no notehead can express says so", "[engraving][value]")
{
    // Five quarters is not any dotted power of two. The largest value that
    // fits is drawn and `exact` is what tells the caller to raise a
    // diagnostic, rather than the difference going unmentioned.
    const NoteValue five = noteValueFor(5 * quarter);

    CHECK(five.notehead == Glyph::noteheadWhole);
    CHECK_FALSE(five.exact);

    // One tick over a quarter: the same rule at the other end of the scale.
    const NoteValue ragged = noteValueFor(quarter + 1);

    CHECK(ragged.flags == 0);
    CHECK_FALSE(ragged.exact);

    // Shorter than the shortest value with a glyph.
    const NoteValue tooShort = noteValueFor(sixteenth / 8);

    CHECK(tooShort.flags == maximumFlags);
    CHECK_FALSE(tooShort.exact);
}

TEST_CASE("a grace note is written as an eighth", "[engraving][value]")
{
    // Zero duration is a grace note (invariant 3), and every engraver sets one
    // as an eighth. That is a decision rather than a failure, so it does not
    // report.
    const NoteValue grace = noteValueFor(0);

    CHECK(grace.flags == 1);
    CHECK(grace.notehead == Glyph::noteheadBlack);
    CHECK(grace.exact);
}
