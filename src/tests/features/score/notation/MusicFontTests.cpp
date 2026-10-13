#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

#include "features/score/notation/MusicFont.h"
#include "platform/score/engraving/GlyphMetrics.h"

// The music font's failure mode is quieter and worse than the UI font's.
//
// If IBM Plex does not load, the application draws in the platform sans and
// looks wrong. If Bravura does not load, notation has nothing to fall back to:
// the codepoints live in the Unicode private use area, so a fallback face draws
// empty boxes, and a page of empty boxes is not something a unit test notices
// on its own.
//
// Same fixture shape as AppLookAndFeelTests, for the same reason -- creating a
// typeface starts JUCE's font subsystem, and without a runtime to own it the
// singletons are still standing at exit and get reported as leaks.

namespace
{
struct MusicFontUnderTest
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
};
} // namespace

TEST_CASE("the embedded music typeface is the one we shipped", "[notation][typeface]")
{
    const MusicFontUnderTest fixture;

    const juce::Typeface::Ptr face = score::notation::musicTypeface();

    REQUIRE(face != nullptr);

    // The name, not merely non-null: a fallback returns a typeface too.
    CHECK(face->getName() == juce::String(score::notation::embeddedMusicFamilyName()));
}

TEST_CASE("every glyph the layout names has a width and a codepoint", "[notation][engraving]")
{
    using namespace score::engraving;

    for (std::size_t index = 0; index < glyphCount; ++index)
    {
        INFO("glyph index " << index);

        // A zero width would silently collapse a column; a zero codepoint would
        // draw nothing. Both are what a generator bug looks like.
        CHECK(glyphAdvanceWidths[index] > 0.0);
        CHECK(glyphCodepoints[index] != U'\0');

        // Every SMuFL glyph lives in the private use area. A codepoint outside
        // it means the table was generated against the wrong metadata.
        CHECK(glyphCodepoints[index] >= U'\ue000');
        CHECK(glyphCodepoints[index] <= U'\uf8ff');
    }
}

TEST_CASE("glyph metrics are the values Bravura states", "[notation][engraving]")
{
    using namespace score::engraving;

    // Spot-checked against Bravura 1.482's metadata rather than against the
    // generator, so that regenerating from a different font fails here instead
    // of quietly changing every column width in the application.
    CHECK(advanceWidth(Glyph::noteheadBlack) == 1.18);
    CHECK(advanceWidth(Glyph::gClef) == 2.684);
    CHECK(codepoint(Glyph::gClef) == U'\ue050');
    CHECK(codepoint(Glyph::noteheadBlack) == U'\ue0a4');

    CHECK(engravingDefaults.staffLineThickness == 0.13);
    CHECK(engravingDefaults.stemThickness == 0.12);
}
