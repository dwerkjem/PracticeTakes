#include <catch2/catch_test_macros.hpp>

#include "features/score/engraving/NoteValue.h"

#include <optional>

using namespace engraving;
using score::Tick;
using score::ticksPerQuarterNote;

namespace
{
constexpr score::TupletRatio plain{1, 1};

[[nodiscard]] WrittenNoteValue valueOf(Tick duration, score::TupletRatio tuplet = plain)
{
    const std::optional<WrittenNoteValue> value = writtenValueFor(duration, tuplet);

    REQUIRE(value.has_value());

    return *value;
}
} // namespace

TEST_CASE("every base value converts from its own duration", "[score][engraving][value]")
{
    struct Expectation
    {
        WrittenValue value;
        Tick ticks;
    };

    // Written out rather than looped over `ticksFor`, so that the test fails if
    // the tick base changes under it rather than agreeing with whatever the
    // code now says.
    const std::vector<Expectation> expected{
        {WrittenValue::breve, 30720},
        {WrittenValue::whole, 15360},
        {WrittenValue::half, 7680},
        {WrittenValue::quarter, 3840},
        {WrittenValue::eighth, 1920},
        {WrittenValue::sixteenth, 960},
        {WrittenValue::thirtySecond, 480},
        {WrittenValue::sixtyFourth, 240},
        {WrittenValue::hundredTwentyEighth, 120}};

    for (const Expectation& expectation : expected)
    {
        INFO(nameOf(expectation.value));

        const WrittenNoteValue converted = valueOf(expectation.ticks);

        CHECK(converted.value == expectation.value);
        CHECK(converted.dots == 0);
        CHECK(ticksFor(expectation.value) == expectation.ticks);
    }
}

TEST_CASE("a dot adds half again, and a second dot half of that", "[score][engraving][value]")
{
    const WrittenNoteValue dottedHalf = valueOf(ticksPerQuarterNote * 3);

    CHECK(dottedHalf.value == WrittenValue::half);
    CHECK(dottedHalf.dots == 1);

    const WrittenNoteValue dottedQuarter = valueOf(ticksPerQuarterNote * 3 / 2);

    CHECK(dottedQuarter.value == WrittenValue::quarter);
    CHECK(dottedQuarter.dots == 1);

    const WrittenNoteValue doubleDottedQuarter = valueOf(ticksPerQuarterNote * 7 / 4);

    CHECK(doubleDottedQuarter.value == WrittenValue::quarter);
    CHECK(doubleDottedQuarter.dots == 2);

    CHECK(ticksFor(WrittenNoteValue{WrittenValue::half, 1}) == ticksPerQuarterNote * 3);
    CHECK(ticksFor(WrittenNoteValue{WrittenValue::whole, 2}) == 26880);
}

// A whole note is twice a half note, so a search that went short-to-long would
// read a dotted value as a shorter one plus dots. Longest first is what stops
// that, and this is the case that would catch it.
TEST_CASE("a plain value is preferred to a dotted shorter one", "[score][engraving][value]")
{
    const WrittenNoteValue whole = valueOf(15360);

    CHECK(whole.value == WrittenValue::whole);
    CHECK(whole.dots == 0);
}

TEST_CASE("a tuplet is written as the value it borrows from", "[score][engraving][value]")
{
    // A triplet eighth lasts two thirds of an eighth and is written as an
    // eighth. Reading its raw duration as a written value would make it a
    // dotted sixteenth, which is a different note.
    const WrittenNoteValue tripletEighth = valueOf(1280, score::TupletRatio{3, 2});

    CHECK(tripletEighth.value == WrittenValue::eighth);
    CHECK(tripletEighth.dots == 0);

    const WrittenNoteValue tripletQuarter = valueOf(2560, score::TupletRatio{3, 2});

    CHECK(tripletQuarter.value == WrittenValue::quarter);

    // Five in the time of four, at a sixteenth: 960 * 4/5 == 768.
    const WrittenNoteValue quintupletSixteenth = valueOf(768, score::TupletRatio{5, 4});

    CHECK(quintupletSixteenth.value == WrittenValue::sixteenth);
}

TEST_CASE("a duration no written value matches is reported", "[score][engraving][value]")
{
    // Not rounded to the nearest. A note drawn as a value it is not is a note
    // the player plays wrongly, where a reported one is a sentence the renderer
    // can show.
    CHECK_FALSE(writtenValueFor(1000, plain).has_value());
    CHECK_FALSE(writtenValueFor(ticksPerQuarterNote * 5, plain).has_value());

    // Triple-dotted. Legal notation, vanishingly rare, and a different duration
    // from a double-dotted value -- so it is reported rather than drawn with
    // two dots.
    CHECK_FALSE(writtenValueFor(ticksPerQuarterNote * 15 / 8, plain).has_value());
}

TEST_CASE("a grace note's zero duration names no value", "[score][engraving][value]")
{
    // Invariant 3: a zero duration means a grace note, whose written value is a
    // convention rather than an arithmetic fact. The caller chooses it.
    CHECK_FALSE(writtenValueFor(0, plain).has_value());
    CHECK_FALSE(writtenValueFor(-1920, plain).has_value());
}

TEST_CASE("a malformed tuplet ratio is reported, not divided by", "[score][engraving][value]")
{
    CHECK_FALSE(writtenValueFor(ticksPerQuarterNote, score::TupletRatio{0, 2}).has_value());
    CHECK_FALSE(writtenValueFor(ticksPerQuarterNote, score::TupletRatio{3, 0}).has_value());

    // A ratio that does not divide the duration exactly means the two disagree,
    // which is reported rather than rounded. MusicalTime.h takes the same view
    // of an inexact rescale one layer down.
    CHECK_FALSE(writtenValueFor(1281, score::TupletRatio{3, 2}).has_value());
}

TEST_CASE("the notehead and flags follow from the value", "[score][engraving][value]")
{
    CHECK(noteheadFor(WrittenValue::breve) == NoteheadKind::breve);
    CHECK(noteheadFor(WrittenValue::whole) == NoteheadKind::whole);
    CHECK(noteheadFor(WrittenValue::half) == NoteheadKind::half);

    for (const WrittenValue filled :
         {WrittenValue::quarter, WrittenValue::eighth, WrittenValue::sixteenth,
          WrittenValue::hundredTwentyEighth})
    {
        INFO(nameOf(filled));
        CHECK(noteheadFor(filled) == NoteheadKind::filled);
    }

    CHECK(flagCountFor(WrittenValue::whole) == 0);
    CHECK(flagCountFor(WrittenValue::quarter) == 0);
    CHECK(flagCountFor(WrittenValue::eighth) == 1);
    CHECK(flagCountFor(WrittenValue::sixteenth) == 2);
    CHECK(flagCountFor(WrittenValue::thirtySecond) == 3);
    CHECK(flagCountFor(WrittenValue::sixtyFourth) == 4);
    CHECK(flagCountFor(WrittenValue::hundredTwentyEighth) == 5);

    // The two values drawn without a stem at all.
    CHECK_FALSE(hasStem(WrittenValue::breve));
    CHECK_FALSE(hasStem(WrittenValue::whole));
    CHECK(hasStem(WrittenValue::half));
    CHECK(hasStem(WrittenValue::quarter));
}

TEST_CASE("an event's duration and tuplet are read together", "[score][engraving][value]")
{
    score::ScoreEvent event;
    event.kind = score::EventKind::note;
    event.duration = 1280;
    event.tuplet = score::TupletRatio{3, 2};

    const std::optional<WrittenNoteValue> value = writtenValueFor(event);

    REQUIRE(value.has_value());
    CHECK(value->value == WrittenValue::eighth);
}
