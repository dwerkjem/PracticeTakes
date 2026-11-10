#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "application/theme/MusicFont.h"
#include "features/score/SmuflEngraving.h"
#include "features/score/engraving/ScoreLayout.h"

#include <optional>

// Which SMuFL glyph draws each of the engraving's own kinds: a notehead, a
// rest, an accidental.
using Catch::Approx;
using smufl::Glyph;

namespace
{
// As in MusicFontTests: the runtime is declared first so it outlives the font.
struct MusicFontUnderTest
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    MusicFont font;
};
} // namespace

TEST_CASE("each notehead kind has its own glyph", "[score][smufl]")
{
    CHECK(smufl::noteheadFor(engraving::NoteheadKind::filled) == Glyph::noteheadBlack);
    CHECK(smufl::noteheadFor(engraving::NoteheadKind::half) == Glyph::noteheadHalf);
    CHECK(smufl::noteheadFor(engraving::NoteheadKind::whole) == Glyph::noteheadWhole);
    CHECK(smufl::noteheadFor(engraving::NoteheadKind::breve) == Glyph::noteheadDoubleWhole);
}

TEST_CASE("each written value has its own rest", "[score][smufl]")
{
    using engraving::WrittenValue;

    CHECK(smufl::restFor(WrittenValue::breve) == Glyph::restDoubleWhole);
    CHECK(smufl::restFor(WrittenValue::whole) == Glyph::restWhole);
    CHECK(smufl::restFor(WrittenValue::half) == Glyph::restHalf);
    CHECK(smufl::restFor(WrittenValue::quarter) == Glyph::restQuarter);
    CHECK(smufl::restFor(WrittenValue::eighth) == Glyph::rest8th);
    CHECK(smufl::restFor(WrittenValue::sixteenth) == Glyph::rest16th);
    CHECK(smufl::restFor(WrittenValue::thirtySecond) == Glyph::rest32nd);
    CHECK(smufl::restFor(WrittenValue::sixtyFourth) == Glyph::rest64th);
    CHECK(smufl::restFor(WrittenValue::hundredTwentyEighth) == Glyph::rest128th);
}

TEST_CASE("each accidental has its own sign, and none has none", "[score][smufl]")
{
    using engraving::AccidentalKind;

    CHECK(smufl::accidentalFor(AccidentalKind::sharp) == Glyph::accidentalSharp);
    CHECK(smufl::accidentalFor(AccidentalKind::flat) == Glyph::accidentalFlat);
    CHECK(smufl::accidentalFor(AccidentalKind::natural) == Glyph::accidentalNatural);
    CHECK(smufl::accidentalFor(AccidentalKind::doubleSharp) == Glyph::accidentalDoubleSharp);
    CHECK(smufl::accidentalFor(AccidentalKind::doubleFlat) == Glyph::accidentalDoubleFlat);
    CHECK_FALSE(smufl::accidentalFor(AccidentalKind::none).has_value());
}

// The engraving's sizes, checked against the font it is drawn with.
TEST_CASE("the layout's notehead is the font's notehead", "[score][typeface]")
{
    // The layout places stems at the edge of a notehead it measures with its
    // own number. If that number and the font disagree, every stem stands off
    // its notehead or cuts into it, and a font change is how they would come to
    // disagree.
    const MusicFontUnderTest fixture;

    CHECK(
        engraving::LayoutOptions{}.noteheadWidth ==
        Approx(fixture.font.bounds(Glyph::noteheadBlack).getRight()).margin(0.01));
}

TEST_CASE("the layout's notehead widths are the font's", "[score][typeface]")
{
    // The layout ends each ledger line past the notehead drawn on it, and a
    // whole note is wider than a black one. A table narrower than the font
    // leaves the head hanging off its line.
    const MusicFontUnderTest fixture;

    for (const engraving::NoteheadKind kind :
         {engraving::NoteheadKind::breve, engraving::NoteheadKind::whole,
          engraving::NoteheadKind::half, engraving::NoteheadKind::filled})
    {
        const juce::Rectangle<float> bounds = fixture.font.bounds(smufl::noteheadFor(kind));

        CHECK(engraving::noteheadWidthFor(kind) == Approx(bounds.getRight()).margin(0.02));
    }
}

TEST_CASE("the layout's accidentals are the font's accidentals", "[score][typeface]")
{
    // The layout stacks a chord's accidentals into columns using its own table
    // of widths and heights, and the painter draws each sign where the layout
    // put it. A table wider than the font leaves gaps; one narrower overlaps a
    // sign with the one beside it, and a height that is short lets two signs in
    // one column touch. Margin as for the notehead: SMuFL publishes three
    // decimal places and the outline's own bounds round differently.
    const MusicFontUnderTest fixture;

    for (const engraving::AccidentalKind kind :
         {engraving::AccidentalKind::natural, engraving::AccidentalKind::sharp,
          engraving::AccidentalKind::flat, engraving::AccidentalKind::doubleSharp,
          engraving::AccidentalKind::doubleFlat})
    {
        INFO(engraving::nameOf(kind));

        const std::optional<Glyph> sign = smufl::accidentalFor(kind);
        REQUIRE(sign.has_value());

        const juce::Rectangle<float> bounds = fixture.font.bounds(*sign);
        const engraving::AccidentalMetrics metrics = engraving::accidentalMetricsFor(kind);

        // Origin at the left edge, which is what makes `accidentalX` the
        // sign's left edge.
        CHECK(bounds.getX() == Approx(0.0).margin(0.02));

        CHECK(metrics.width == Approx(bounds.getRight()).margin(0.02));

        // Staff spaces grow downwards in the font as on the page, so the part
        // above the origin is the negative part.
        CHECK(metrics.above == Approx(-bounds.getY()).margin(0.02));
        CHECK(metrics.below == Approx(bounds.getBottom()).margin(0.02));
    }
}
