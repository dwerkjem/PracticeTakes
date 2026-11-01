#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "features/score/MusicFont.h"
#include "features/score/engraving/ScoreLayout.h"

#include <cmath>

// The embedded music font, checked against what the layout assumes of it.
//
// A music font fails more quietly than a UI typeface. A missing UI face falls
// back to the platform sans and the application merely looks different; a
// missing SMuFL glyph is a Private Use Area codepoint, which every fallback
// draws as nothing. So these check the family, that every glyph the renderer
// asks for is present, and -- the part that matters most -- that the unit
// conversion is right, by comparing a notehead against the size SMuFL
// publishes for it.
using Catch::Approx;
using smufl::Glyph;

namespace
{
// Same shape and same reason as LookAndFeelUnderTest in AppLookAndFeelTests:
// creating a typeface starts JUCE's font subsystem, whose singletons are still
// standing at exit without a runtime to own them, and JUCE reports each as a
// leak. The runtime is declared first so it outlives the font.
struct MusicFontUnderTest
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    MusicFont font;
};
} // namespace

TEST_CASE("the embedded music font is the one we shipped", "[score][typeface]")
{
    const MusicFontUnderTest fixture;

    REQUIRE(fixture.font.isLoaded());
    CHECK(fixture.font.familyName() == "Bravura");
}

TEST_CASE("every glyph the renderer asks for is in the font", "[score][typeface]")
{
    // One assertion per glyph, so a failure names the codepoint that went
    // missing rather than reporting that something did.
    const MusicFontUnderTest fixture;

    for (const Glyph glyph : smufl::allGlyphs())
    {
        INFO(
            "U+" << juce::String::toHexString(static_cast<int>(smufl::codepointOf(glyph)))
                        .toUpperCase());
        CHECK(fixture.font.hasGlyph(glyph));
        CHECK_FALSE(fixture.font.bounds(glyph).isEmpty());
    }
}

TEST_CASE("one em of the music font is four staff spaces", "[score][typeface]")
{
    // The whole of the unit conversion, pinned against a published number:
    // Bravura's black notehead is 1.18 staff spaces wide and exactly one staff
    // space tall, centred on the baseline. Get the em-to-space factor wrong and
    // every glyph on the page is the wrong size by the same ratio, with nothing
    // else failing.
    const MusicFontUnderTest fixture;

    const juce::Rectangle<float> head = fixture.font.bounds(Glyph::noteheadBlack);

    CHECK(head.getX() == Approx(0.0).margin(0.01));
    CHECK(head.getRight() == Approx(1.18).margin(0.01));
    CHECK(head.getY() == Approx(-0.5).margin(0.01));
    CHECK(head.getBottom() == Approx(0.5).margin(0.01));
}

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

TEST_CASE("glyphs keep SMuFL's origins", "[score][typeface]")
{
    // Every glyph is placed by translating its origin to a staff position the
    // layout already computed. These are the conventions that makes work, and a
    // glyph with the wrong one is drawn a fixed distance from where it belongs.
    const MusicFontUnderTest fixture;
    const MusicFont& font = fixture.font;

    SECTION("noteheads are centred on the line or space they sit on")
    {
        for (const Glyph head :
             {Glyph::noteheadBlack, Glyph::noteheadHalf, Glyph::noteheadWhole,
              Glyph::noteheadDoubleWhole})
        {
            const juce::Rectangle<float> bounds = font.bounds(head);

            CHECK(bounds.getCentreY() == Approx(0.0).margin(0.02));
            CHECK(bounds.getX() == Approx(0.0).margin(0.02));
        }
    }

    SECTION("an up-stem flag hangs down from the stem's tip, a down-stem one rises")
    {
        // Which is also what tells the two members of each pair apart: a
        // codepoint table with them swapped would pass every presence check.
        for (int flags = 1; flags <= 5; ++flags)
        {
            INFO(flags << " flags");

            const juce::Rectangle<float> up = font.bounds(*smufl::flagFor(flags, true));
            const juce::Rectangle<float> down = font.bounds(*smufl::flagFor(flags, false));

            CHECK(up.getBottom() > 2.0f);
            CHECK(up.getCentreY() > 0.0f);
            CHECK(down.getY() < -2.0f);
            CHECK(down.getCentreY() < 0.0f);
        }
    }

    SECTION("a whole rest hangs from its line and a half rest sits on its line")
    {
        // The two shapes are the same block; which side of the line it is on
        // is the only difference a reader sees.
        const juce::Rectangle<float> whole = font.bounds(Glyph::restWhole);
        const juce::Rectangle<float> half = font.bounds(Glyph::restHalf);

        CHECK(whole.getY() == Approx(0.0).margin(0.05));
        CHECK(whole.getBottom() > 0.4f);

        CHECK(half.getBottom() == Approx(0.0).margin(0.05));
        CHECK(half.getY() < -0.4f);
    }

    SECTION("clefs sit on the line they name")
    {
        // A treble clef's curl is around its G line and its body rises far
        // above it; a bass clef's dots flank its F line and its body falls
        // below; a C clef is symmetric about its C line.
        CHECK(font.bounds(Glyph::gClef).getY() < -4.0f);
        CHECK(font.bounds(Glyph::fClef).getBottom() > 2.0f);
        CHECK(font.bounds(Glyph::fClef).getY() > -1.5f);
        CHECK(font.bounds(Glyph::cClef).getCentreY() == Approx(0.0).margin(0.05));
    }

    SECTION("an octave clef carries its eight on the side it names")
    {
        CHECK(font.bounds(Glyph::gClef8vb).getBottom() > font.bounds(Glyph::gClef).getBottom());
        CHECK(font.bounds(Glyph::gClef8va).getY() < font.bounds(Glyph::gClef).getY());
        CHECK(font.bounds(Glyph::fClef8vb).getBottom() > font.bounds(Glyph::fClef).getBottom());
        CHECK(font.bounds(Glyph::fClef8va).getY() < font.bounds(Glyph::fClef).getY());
    }

    SECTION("time signature digits are centred on the line they straddle")
    {
        for (int digit = 0; digit <= 9; ++digit)
        {
            const juce::Rectangle<float> bounds =
                font.bounds(smufl::timeSignatureDigits(digit).front());

            INFO("digit " << digit);
            CHECK(bounds.getCentreY() == Approx(0.0).margin(0.05));

            // Two spaces tall: a time signature's numbers each fill half the
            // staff.
            CHECK(bounds.getHeight() == Approx(2.0).margin(0.1));
        }
    }

    SECTION("accidentals are centred near the note they modify")
    {
        // A flat's bowl, not its stem, is what sits on the note, so it is the
        // one sign whose centre is well above its origin.
        CHECK(font.bounds(Glyph::accidentalSharp).getCentreY() == Approx(0.0).margin(0.05));
        CHECK(font.bounds(Glyph::accidentalNatural).getCentreY() == Approx(0.0).margin(0.05));
        CHECK(font.bounds(Glyph::accidentalFlat).getBottom() < 1.0f);
        CHECK(font.bounds(Glyph::accidentalFlat).getY() < -1.5f);
    }
}

TEST_CASE("an absent glyph is empty rather than a crash", "[score][typeface]")
{
    const MusicFontUnderTest fixture;

    // Not a SMuFL glyph this table lists, so the font was never asked for it.
    const auto unlisted = static_cast<Glyph>(0xE000);

    CHECK_FALSE(fixture.font.hasGlyph(unlisted));
    CHECK(fixture.font.outline(unlisted).isEmpty());
    CHECK(fixture.font.bounds(unlisted).isEmpty());
}
