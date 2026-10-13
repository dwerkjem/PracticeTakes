#include <catch2/catch_test_macros.hpp>

#include "platform/score/engraving/Accidentals.h"

using namespace score;
using namespace score::engraving;

namespace
{
[[nodiscard]] KeySignature keyOf(int fifths)
{
    KeySignature key;
    key.fifths = fifths;

    return key;
}
} // namespace

TEST_CASE("a key signature sharpens in circle-of-fifths order", "[engraving][accidentals]")
{
    // D major: two sharps, F and C, and nothing else.
    const KeySignature twoSharps = keyOf(2);

    CHECK(keyAlterationFor(Step::f, twoSharps) == 1);
    CHECK(keyAlterationFor(Step::c, twoSharps) == 1);
    CHECK(keyAlterationFor(Step::g, twoSharps) == 0);
    CHECK(keyAlterationFor(Step::b, twoSharps) == 0);
}

TEST_CASE("a key signature flattens in its own order", "[engraving][accidentals]")
{
    // B-flat major: two flats, B and E.
    const KeySignature twoFlats = keyOf(-2);

    CHECK(keyAlterationFor(Step::b, twoFlats) == -1);
    CHECK(keyAlterationFor(Step::e, twoFlats) == -1);
    CHECK(keyAlterationFor(Step::a, twoFlats) == 0);
    CHECK(keyAlterationFor(Step::f, twoFlats) == 0);
}

TEST_CASE("C major alters nothing", "[engraving][accidentals]")
{
    const KeySignature none = keyOf(0);

    CHECK(keyAlterationFor(Step::f, none) == 0);
    CHECK(keyAlterationFor(Step::b, none) == 0);
}

TEST_CASE("a note matching the key signature needs no accidental", "[engraving][accidentals]")
{
    AccidentalState state;
    state.setKey(keyOf(2));

    // F sharp in D major is what the signature already says.
    CHECK_FALSE(state.accidentalFor(pitchFromSpelling(Step::f, 1, 5)).has_value());
}

TEST_CASE("a note contradicting the key signature gets an accidental", "[engraving][accidentals]")
{
    AccidentalState state;
    state.setKey(keyOf(2));

    // F natural in D major has to say so.
    const std::optional<Glyph> natural = state.accidentalFor(pitchFromSpelling(Step::f, 0, 5));

    REQUIRE(natural.has_value());
    CHECK(*natural == Glyph::accidentalNatural);
}

TEST_CASE("an accidental is not repeated within its measure", "[engraving][accidentals]")
{
    AccidentalState state;
    state.setKey(keyOf(0));
    state.beginMeasure();

    const Pitch fSharp = pitchFromSpelling(Step::f, 1, 5);

    const std::optional<Glyph> first = state.accidentalFor(fSharp);

    REQUIRE(first.has_value());
    CHECK(*first == Glyph::accidentalSharp);

    // The same note again in the same bar is already covered.
    CHECK_FALSE(state.accidentalFor(fSharp).has_value());
}

TEST_CASE("an accidental expires at the barline", "[engraving][accidentals]")
{
    AccidentalState state;
    state.setKey(keyOf(0));
    state.beginMeasure();

    const Pitch fSharp = pitchFromSpelling(Step::f, 1, 5);

    REQUIRE(state.accidentalFor(fSharp).has_value());

    state.beginMeasure();

    // The new bar has to say it again. Carrying it over is the mistake that
    // reads as a plausible optimisation and misspells the rest of the page.
    CHECK(state.accidentalFor(fSharp).has_value());
}

TEST_CASE("an accidental applies to one octave only", "[engraving][accidentals]")
{
    AccidentalState state;
    state.setKey(keyOf(0));
    state.beginMeasure();

    REQUIRE(state.accidentalFor(pitchFromSpelling(Step::f, 1, 5)).has_value());

    // F sharp an octave lower is a different line and needs its own sharp.
    CHECK(state.accidentalFor(pitchFromSpelling(Step::f, 1, 4)).has_value());
}

TEST_CASE("cancelling an accidental within a measure needs a natural", "[engraving][accidentals]")
{
    AccidentalState state;
    state.setKey(keyOf(0));
    state.beginMeasure();

    REQUIRE(state.accidentalFor(pitchFromSpelling(Step::f, 1, 5)).has_value());

    const std::optional<Glyph> cancel = state.accidentalFor(pitchFromSpelling(Step::f, 0, 5));

    REQUIRE(cancel.has_value());
    CHECK(*cancel == Glyph::accidentalNatural);

    // And having cancelled, the natural is now what is in force, so a second
    // F natural says nothing.
    CHECK_FALSE(state.accidentalFor(pitchFromSpelling(Step::f, 0, 5)).has_value());
}

TEST_CASE("a key change clears what the previous bar had in force", "[engraving][accidentals]")
{
    AccidentalState state;
    state.setKey(keyOf(0));
    state.beginMeasure();

    REQUIRE(state.accidentalFor(pitchFromSpelling(Step::f, 1, 5)).has_value());

    state.setKey(keyOf(2));

    // F sharp is now the signature's, so it draws nothing -- but only because
    // the key says so, not because the old accidental lingered.
    CHECK_FALSE(state.accidentalFor(pitchFromSpelling(Step::f, 1, 5)).has_value());
    CHECK(state.alterationInForceFor(pitchFromSpelling(Step::f, 0, 5)) == 1);
}

TEST_CASE("double accidentals have their own glyphs", "[engraving][accidentals]")
{
    CHECK(accidentalGlyphFor(2) == Glyph::accidentalDoubleSharp);
    CHECK(accidentalGlyphFor(-2) == Glyph::accidentalDoubleFlat);
    CHECK_FALSE(accidentalGlyphFor(3).has_value());
}
