#include <algorithm>
#include <cmath>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "platform/score/engraving/SystemHead.h"

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

// The steps a key signature's accidentals occupy, in written order.
[[nodiscard]] std::vector<int> stepsOf(const std::vector<PlacedGlyph>& glyphs)
{
    std::vector<int> steps;
    steps.reserve(glyphs.size());

    for (const PlacedGlyph& glyph : glyphs)
    {
        steps.push_back(glyph.staffStep);
    }

    return steps;
}

[[nodiscard]] std::vector<PlacedGlyph>
glyphsAtStep(const std::vector<PlacedGlyph>& glyphs, int staffStep)
{
    std::vector<PlacedGlyph> matching;

    for (const PlacedGlyph& glyph : glyphs)
    {
        if (glyph.staffStep == staffStep)
        {
            matching.push_back(glyph);
        }
    }

    return matching;
}
} // namespace

TEST_CASE("a clef sign chooses its glyph and its line", "[engraving][head]")
{
    CHECK(clefGlyphFor(trebleClef()).glyph == Glyph::gClef);
    CHECK(clefGlyphFor(trebleClef()).staffStep == 2);

    CHECK(clefGlyphFor(bassClef()).glyph == Glyph::fClef);
    CHECK(clefGlyphFor(bassClef()).staffStep == 6);

    CHECK(clefGlyphFor(altoClef()).glyph == Glyph::cClef);
    CHECK(clefGlyphFor(altoClef()).staffStep == 4);
}

TEST_CASE("a staff with no pitched reading takes no key signature", "[engraving][head]")
{
    const Clef percussion{"percussion", 3, 0, 1};

    CHECK_FALSE(clefGlyphFor(percussion).hasPitchedStaff);

    // It still gets a clef glyph -- something is drawn -- but five sharps on a
    // drum staff would be meaningless rather than approximate.
    CHECK(keySignatureGlyphs(KeySignature{5, "major"}, percussion).empty());
    CHECK(
        systemHeadFor(percussion, KeySignature{5, "major"}, TimeSignature{4, 4}).glyphs.size() ==
        1 + 2);
}

TEST_CASE("key signature accidentals sit where engraving convention puts them", "[engraving][head]")
{
    // The positions every published score uses, written out rather than
    // derived: this is convention, so a formula that produced something else
    // would be wrong even if it were self-consistent.
    //
    // Treble sharps run F5 C5 G5 D5 A4 E5 B4 -- note that the third sharp sits
    // in the space *above* the top line, which is the case a naive
    // "keep it inside the staff" rule gets wrong.
    CHECK(
        stepsOf(keySignatureGlyphs(KeySignature{7, "major"}, trebleClef())) ==
        std::vector<int>{8, 5, 9, 6, 3, 7, 4});

    CHECK(
        stepsOf(keySignatureGlyphs(KeySignature{7, "major"}, bassClef())) ==
        std::vector<int>{6, 3, 7, 4, 1, 5, 2});

    CHECK(
        stepsOf(keySignatureGlyphs(KeySignature{7, "major"}, altoClef())) ==
        std::vector<int>{7, 4, 8, 5, 2, 6, 3});

    // Treble flats run B4 E5 A4 D5 G4 C5 F4.
    CHECK(
        stepsOf(keySignatureGlyphs(KeySignature{-7, "major"}, trebleClef())) ==
        std::vector<int>{4, 7, 3, 6, 2, 5, 1});

    CHECK(
        stepsOf(keySignatureGlyphs(KeySignature{-7, "major"}, bassClef())) ==
        std::vector<int>{2, 5, 1, 4, 0, 3, -1});

    CHECK(
        stepsOf(keySignatureGlyphs(KeySignature{-7, "major"}, altoClef())) ==
        std::vector<int>{3, 6, 2, 5, 1, 4, 0});
}

TEST_CASE("a key signature draws as many accidentals as it has", "[engraving][head]")
{
    CHECK(keySignatureGlyphs(KeySignature{0, "major"}, trebleClef()).empty());

    const std::vector<PlacedGlyph> threeSharps =
        keySignatureGlyphs(KeySignature{3, "major"}, trebleClef());

    REQUIRE(threeSharps.size() == 3);

    for (const PlacedGlyph& glyph : threeSharps)
    {
        CHECK(glyph.glyph == Glyph::accidentalSharp);
    }

    // A prefix of the seven, in the same order -- the first three sharps of A
    // major are the first three of C sharp major.
    CHECK(stepsOf(threeSharps) == std::vector<int>{8, 5, 9});

    const std::vector<PlacedGlyph> twoFlats =
        keySignatureGlyphs(KeySignature{-2, "major"}, trebleClef());

    REQUIRE(twoFlats.size() == 2);

    for (const PlacedGlyph& glyph : twoFlats)
    {
        CHECK(glyph.glyph == Glyph::accidentalFlat);
    }

    // More than seven is a wrong file rather than an interesting one, and is
    // clamped rather than running off the staff.
    CHECK(keySignatureGlyphs(KeySignature{9, "major"}, trebleClef()).size() == 7);
}

TEST_CASE(
    "key signature accidentals advance left to right without overlapping",
    "[engraving][head]")
{
    const std::vector<PlacedGlyph> sharps =
        keySignatureGlyphs(KeySignature{4, "major"}, trebleClef());

    REQUIRE(sharps.size() == 4);

    for (std::size_t index = 1; index < sharps.size(); ++index)
    {
        CHECK(sharps[index].x >= sharps[index - 1].x + advanceWidth(sharps[index - 1].glyph));
    }
}

TEST_CASE("a time signature stacks its digits on the staff", "[engraving][head]")
{
    const std::vector<PlacedGlyph> common = timeSignatureGlyphs(TimeSignature{4, 4});

    REQUIRE(common.size() == 2);

    // Numerator above denominator, both centred vertically in their half of the
    // staff, and at the same x because they are the same width.
    CHECK(common[0].glyph == Glyph::timeSig4);
    CHECK(common[1].glyph == Glyph::timeSig4);
    CHECK(common[0].staffStep > common[1].staffStep);
    CHECK(common[0].x == common[1].x);
}

TEST_CASE("a two-digit time signature centres its numbers on each other", "[engraving][head]")
{
    const std::vector<PlacedGlyph> twelveEight = timeSignatureGlyphs(TimeSignature{12, 8});

    // One, two, eight.
    REQUIRE(twelveEight.size() == 3);

    CHECK(twelveEight[0].glyph == Glyph::timeSig1);
    CHECK(twelveEight[1].glyph == Glyph::timeSig2);
    CHECK(twelveEight[2].glyph == Glyph::timeSig8);

    const int numeratorStep = twelveEight.front().staffStep;
    const int denominatorStep = twelveEight.back().staffStep;

    REQUIRE(numeratorStep > denominatorStep);

    const std::vector<PlacedGlyph> numerator = glyphsAtStep(twelveEight, numeratorStep);
    const std::vector<PlacedGlyph> denominator = glyphsAtStep(twelveEight, denominatorStep);

    REQUIRE(numerator.size() == 2);
    REQUIRE(denominator.size() == 1);

    const double numeratorLeft = numerator.front().x;
    const double numeratorRight = numerator.back().x + advanceWidth(numerator.back().glyph);

    const double denominatorLeft = denominator.front().x;
    const double denominatorRight = denominatorLeft + advanceWidth(denominator.front().glyph);

    // The 8 sits under the middle of the 12, not under the 1.
    const double numeratorCentre = (numeratorLeft + numeratorRight) * 0.5;
    const double denominatorCentre = (denominatorLeft + denominatorRight) * 0.5;

    CHECK(std::abs(numeratorCentre - denominatorCentre) < 0.001);
}

TEST_CASE("a head reads clef, then key, then time", "[engraving][head]")
{
    const SystemHead head =
        systemHeadFor(trebleClef(), KeySignature{2, "major"}, TimeSignature{3, 4});

    // Clef, two sharps, two digits.
    REQUIRE(head.glyphs.size() == 5);

    CHECK(head.glyphs[0].glyph == Glyph::gClef);
    CHECK(head.glyphs[1].glyph == Glyph::accidentalSharp);
    CHECK(head.glyphs[2].glyph == Glyph::accidentalSharp);
    CHECK(head.glyphs[3].glyph == Glyph::timeSig3);
    CHECK(head.glyphs[4].glyph == Glyph::timeSig4);

    // Each element entirely to the right of the one before it.
    CHECK(head.glyphs[1].x > head.glyphs[0].x + advanceWidth(Glyph::gClef));
    CHECK(head.glyphs[3].x > head.glyphs[2].x + advanceWidth(Glyph::accidentalSharp));

    // And the music starts to the right of all of it.
    CHECK(head.width > head.glyphs[4].x + advanceWidth(Glyph::timeSig4));
}

TEST_CASE("a head is only as wide as what it holds", "[engraving][head]")
{
    const SystemHead bare = systemHeadFor(trebleClef(), std::nullopt, std::nullopt);
    const SystemHead keyOnly = systemHeadFor(trebleClef(), KeySignature{0, "major"}, std::nullopt);
    const SystemHead sevenSharps =
        systemHeadFor(trebleClef(), KeySignature{7, "major"}, TimeSignature{4, 4});

    // A clef is always drawn, so a head is never empty.
    REQUIRE(bare.glyphs.size() == 1);
    CHECK(bare.width > advanceWidth(Glyph::gClef));

    // C major asks for no accidentals, so it costs nothing.
    CHECK(keyOnly.width == bare.width);

    // Seven sharps and a time signature need considerably more room than the
    // fixed nine staff spaces this used to reserve, which is why the width is
    // computed rather than assumed.
    CHECK(sevenSharps.width > 9.0);
}
