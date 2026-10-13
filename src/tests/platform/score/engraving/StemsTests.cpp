#include <catch2/catch_test_macros.hpp>

#include "platform/score/engraving/Stems.h"

using namespace score::engraving;

TEST_CASE("stem direction follows the note furthest from the middle line", "[engraving][stem]")
{
    // Below the middle line points up, above points down. Stated at the
    // extremes first, because this is the rule everything else is an exception
    // to.
    CHECK(stemPointsUp(0, 0));
    CHECK(stemPointsUp(1, 1));
    CHECK(stemPointsUp(3, 3));

    CHECK_FALSE(stemPointsUp(5, 5));
    CHECK_FALSE(stemPointsUp(8, 8));
    CHECK_FALSE(stemPointsUp(12, 12));
}

TEST_CASE("a note on the middle line stems down", "[engraving][stem]")
{
    // The one tie, and the one worth pinning: either answer looks plausible in
    // code, and the convention is down.
    CHECK_FALSE(stemPointsUp(middleLineStep, middleLineStep));
}

TEST_CASE("a chord's direction comes from its outermost note", "[engraving][stem]")
{
    // Low note three steps below the middle, high note one above: the low one
    // is further, so up.
    CHECK(stemPointsUp(1, 5));

    // And the other way round.
    CHECK_FALSE(stemPointsUp(3, 7));

    // Straddling the middle line evenly is the tie again, and goes down.
    CHECK_FALSE(stemPointsUp(2, 6));
}

TEST_CASE(
    "an up stem is on the right of the notehead and a down stem on the left",
    "[engraving][stem]")
{
    const double noteheadX = 10.0;
    const double width = advanceWidth(Glyph::noteheadBlack);

    const StemGeometry up = stemFor(noteheadX, Glyph::noteheadBlack, 0, 0, true);
    const StemGeometry down = stemFor(noteheadX, Glyph::noteheadBlack, 8, 8, false);

    // Flush with the notehead's own edges, half a stem thickness inside each.
    CHECK(up.x > noteheadX + width - engravingDefaults.stemThickness);
    CHECK(up.x <= noteheadX + width);

    CHECK(down.x >= noteheadX);
    CHECK(down.x < noteheadX + engravingDefaults.stemThickness);
}

TEST_CASE("a stem is an octave from the note it attaches to", "[engraving][stem]")
{
    // An up stem attaches at the lowest note of the chord, and its length is
    // measured from *that* note. Measuring from the far one instead gives a
    // four-note chord a stem half the height of the system, which is what the
    // first capture of this showed.
    const StemGeometry up = stemFor(0.0, Glyph::noteheadBlack, 0, 4, true);

    CHECK(up.fromStep == 0.0);
    CHECK(up.toStep == defaultStemSteps);

    const StemGeometry down = stemFor(0.0, Glyph::noteheadBlack, 4, 8, false);

    CHECK(down.fromStep == 8.0);
    CHECK(down.toStep == 8 - defaultStemSteps);
}

TEST_CASE("a chord wider than an octave still clears its far note", "[engraving][stem]")
{
    // Ten steps is wider than the octave the default length gives, so the stem
    // is extended rather than ending inside the chord.
    const StemGeometry up = stemFor(0.0, Glyph::noteheadBlack, 0, 10, true);

    CHECK(up.toStep == 10 + chordOvershootSteps);
    CHECK(up.toStep > up.fromStep + defaultStemSteps);

    const StemGeometry down = stemFor(0.0, Glyph::noteheadBlack, 0, 10, false);

    CHECK(down.toStep == 0 - chordOvershootSteps);
}

TEST_CASE("a note far outside the staff still reaches it", "[engraving][stem]")
{
    // Three ledger lines below. A nominal seven-step stem would stop short of
    // the staff and read as floating, so the free end is pulled to the middle
    // line.
    const StemGeometry up = stemFor(0.0, Glyph::noteheadBlack, -12, -12, true);

    CHECK(up.toStep == static_cast<double>(middleLineStep));
    CHECK(up.toStep - up.fromStep > defaultStemSteps);

    const StemGeometry down = stemFor(0.0, Glyph::noteheadBlack, 20, 20, false);

    CHECK(down.toStep == static_cast<double>(middleLineStep));
    CHECK(down.fromStep - down.toStep > defaultStemSteps);
}

TEST_CASE("a flag matches the direction of its stem", "[engraving][stem]")
{
    CHECK(flagGlyphFor(1, true) == Glyph::flag8thUp);
    CHECK(flagGlyphFor(1, false) == Glyph::flag8thDown);
    CHECK(flagGlyphFor(2, true) == Glyph::flag16thUp);
    CHECK(flagGlyphFor(2, false) == Glyph::flag16thDown);

    // Nothing shorter has a glyph in the metrics table, so a thirty-second
    // reuses the sixteenth's. Drawn and reported, rather than drawn as an
    // eighth and left looking deliberate.
    CHECK(flagGlyphFor(3, true) == Glyph::flag16thUp);
    CHECK(flagGlyphFor(4, false) == Glyph::flag16thDown);
}
