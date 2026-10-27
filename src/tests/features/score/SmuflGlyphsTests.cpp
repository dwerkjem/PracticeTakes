#include <catch2/catch_test_macros.hpp>

#include "features/score/SmuflGlyphs.h"

#include <algorithm>
#include <set>
#include <vector>

// Which SMuFL glyph draws each thing the layout names.
//
// These are facts about notation and about the SMuFL specification, checked
// without a font: that a sixteenth's up-stem takes `flag16thUp`, that a vocal
// tenor part's clef carries its eight. MusicFontTests checks the other half --
// that the font actually has a glyph at each codepoint, and that its shape is
// the one the name promises.
using smufl::Glyph;

TEST_CASE("each notehead kind has its own glyph", "[score][smufl]")
{
    CHECK(smufl::noteheadFor(engraving::NoteheadKind::filled) == Glyph::noteheadBlack);
    CHECK(smufl::noteheadFor(engraving::NoteheadKind::half) == Glyph::noteheadHalf);
    CHECK(smufl::noteheadFor(engraving::NoteheadKind::whole) == Glyph::noteheadWhole);
    CHECK(smufl::noteheadFor(engraving::NoteheadKind::breve) == Glyph::noteheadDoubleWhole);
}

TEST_CASE("a flag is one glyph per count and direction", "[score][smufl]")
{
    SECTION("each count, up and down, by name")
    {
        CHECK(smufl::flagFor(1, true) == Glyph::flag8thUp);
        CHECK(smufl::flagFor(1, false) == Glyph::flag8thDown);
        CHECK(smufl::flagFor(2, true) == Glyph::flag16thUp);
        CHECK(smufl::flagFor(2, false) == Glyph::flag16thDown);
        CHECK(smufl::flagFor(3, true) == Glyph::flag32ndUp);
        CHECK(smufl::flagFor(3, false) == Glyph::flag32ndDown);
        CHECK(smufl::flagFor(4, true) == Glyph::flag64thUp);
        CHECK(smufl::flagFor(4, false) == Glyph::flag64thDown);
        CHECK(smufl::flagFor(5, true) == Glyph::flag128thUp);
        CHECK(smufl::flagFor(5, false) == Glyph::flag128thDown);
    }

    SECTION("nothing for a value that takes no flag, or one beyond the table")
    {
        CHECK_FALSE(smufl::flagFor(0, true).has_value());
        CHECK_FALSE(smufl::flagFor(-1, false).has_value());
        CHECK_FALSE(smufl::flagFor(6, true).has_value());
    }
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

TEST_CASE("a clef's glyph carries its octave change", "[score][smufl]")
{
    const auto clefOf = [](const char* sign, int octaveChange)
    { return score::Clef{sign, 2, octaveChange, 1}; };

    SECTION("plain clefs")
    {
        CHECK(smufl::clefFor(clefOf("G", 0))->glyph == Glyph::gClef);
        CHECK(smufl::clefFor(clefOf("F", 0))->glyph == Glyph::fClef);
        CHECK(smufl::clefFor(clefOf("C", 0))->glyph == Glyph::cClef);
    }

    SECTION("a vocal tenor part's treble clef shows its eight below")
    {
        // The case octave clefs exist for. Drawn as a plain treble clef it
        // claims a register an octave higher than the part sounds.
        const std::optional<smufl::ClefGlyphChoice> choice = smufl::clefFor(clefOf("G", -1));

        REQUIRE(choice.has_value());
        CHECK(choice->glyph == Glyph::gClef8vb);
        CHECK(choice->octaveDrawn);
    }

    SECTION("every octave change SMuFL has a glyph for")
    {
        CHECK(smufl::clefFor(clefOf("G", 1))->glyph == Glyph::gClef8va);
        CHECK(smufl::clefFor(clefOf("G", -2))->glyph == Glyph::gClef15mb);
        CHECK(smufl::clefFor(clefOf("G", 2))->glyph == Glyph::gClef15ma);
        CHECK(smufl::clefFor(clefOf("F", -1))->glyph == Glyph::fClef8vb);
        CHECK(smufl::clefFor(clefOf("F", 1))->glyph == Glyph::fClef8va);
        CHECK(smufl::clefFor(clefOf("F", -2))->glyph == Glyph::fClef15mb);
        CHECK(smufl::clefFor(clefOf("F", 2))->glyph == Glyph::fClef15ma);
        CHECK(smufl::clefFor(clefOf("C", -1))->glyph == Glyph::cClef8vb);
    }

    SECTION("an octave change with no glyph says so rather than being dropped")
    {
        // A C clef an octave up has no SMuFL glyph. The plain clef comes back,
        // marked as not showing the octave, so the caller can mark it another
        // way instead of drawing a clef that names the wrong register.
        const std::optional<smufl::ClefGlyphChoice> choice = smufl::clefFor(clefOf("C", 1));

        REQUIRE(choice.has_value());
        CHECK(choice->glyph == Glyph::cClef);
        CHECK_FALSE(choice->octaveDrawn);

        CHECK_FALSE(smufl::clefFor(clefOf("G", 3))->octaveDrawn);
    }

    SECTION("a clef that names no pitch has no glyph here")
    {
        CHECK_FALSE(smufl::clefFor(clefOf("percussion", 0)).has_value());
        CHECK_FALSE(smufl::clefFor(clefOf("TAB", 0)).has_value());
        CHECK_FALSE(smufl::clefFor(clefOf("none", 0)).has_value());
    }
}

TEST_CASE("a time signature number is drawn digit by digit", "[score][smufl]")
{
    CHECK(smufl::timeSignatureDigits(4) == std::vector<Glyph>{Glyph::timeSig4});
    CHECK(smufl::timeSignatureDigits(0) == std::vector<Glyph>{Glyph::timeSig0});
    CHECK(smufl::timeSignatureDigits(9) == std::vector<Glyph>{Glyph::timeSig9});

    // Most significant first, which is left to right on the page.
    CHECK(smufl::timeSignatureDigits(12) == std::vector<Glyph>{Glyph::timeSig1, Glyph::timeSig2});
    CHECK(smufl::timeSignatureDigits(16) == std::vector<Glyph>{Glyph::timeSig1, Glyph::timeSig6});

    CHECK(smufl::timeSignatureDigits(-3).empty());
}

TEST_CASE("the glyph list names every glyph once", "[score][smufl]")
{
    // MusicFontTests checks the font against this list, so a glyph the
    // mapping can return but the list omits is a glyph nothing checks.
    const std::vector<Glyph>& all = smufl::allGlyphs();
    const std::set<Glyph> unique(all.begin(), all.end());

    CHECK(unique.size() == all.size());

    const auto listed = [&unique](Glyph glyph) { return unique.count(glyph) == 1; };

    for (int flags = 1; flags <= 5; ++flags)
    {
        CHECK(listed(*smufl::flagFor(flags, true)));
        CHECK(listed(*smufl::flagFor(flags, false)));
    }

    for (int digit = 0; digit <= 9; ++digit)
    {
        CHECK(listed(smufl::timeSignatureDigits(digit).front()));
    }

    for (const int octave : {-2, -1, 0, 1, 2})
    {
        for (const char* sign : {"G", "F", "C"})
        {
            CHECK(listed(smufl::clefFor(score::Clef{sign, 2, octave, 1})->glyph));
        }
    }

    CHECK(listed(Glyph::augmentationDot));
    CHECK(listed(Glyph::repeatDot));
}
