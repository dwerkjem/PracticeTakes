#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <string>

#include "platform/score/musicxml/MusicXmlImporter.h"
#include "support/MusicXmlFixtures.h"

// The voice cursor and the time base -- the part of MusicXML most likely to
// produce a score that is wrong without being broken.
//
// A mishandled <backup> shifts an entire voice by a fraction of a bar and
// nothing crashes; the score just plays wrong. Nothing here checks that the
// importer "did not crash". Every case asserts the exact tick a note landed on,
// because that is the only assertion a silent shift cannot pass.
using namespace score;
using namespace score::musicxml;
using namespace testing::musicxml;

namespace
{
constexpr Tick quarter = ticksPerQuarterNote;
constexpr Tick half = quarter * 2;
constexpr Tick whole = quarter * 4;

std::shared_ptr<const Score> importOrFail(const std::string& document)
{
    const MusicXmlImportResult result = importMusicXmlDocument(document);

    REQUIRE(succeeded(result.status));
    REQUIRE(result.score != nullptr);

    return result.score;
}

const Voice& voiceNumbered(const Measure& measure, int number)
{
    for (const Voice& voice : measure.voices)
    {
        if (voice.number == number)
        {
            return voice;
        }
    }

    FAIL("the measure has no voice numbered " << number);

    return measure.voices.front();
}
} // namespace

TEST_CASE("two voices written with a backup land on the same beats", "[score][musicxml][timing]")
{
    // Voice 1: four quarter notes. Then back up a whole bar and write voice 2
    // as two half notes over the same span. This is how every notation program
    // writes a two-voice measure, and getting it wrong shifts voice 2.
    const std::string body = measure(
        "1", attributes(1, 4, 4) + note("C", 5, 1) + note("D", 5, 1) + note("E", 5, 1) +
                 note("F", 5, 1) + backup(4) + note("C", 4, 2, 2) + note("E", 4, 2, 2));

    const auto score = importOrFail(scoreDocument(body));
    const Measure& bar = score->parts.front().measures.front();

    REQUIRE(bar.voices.size() == 2);

    const Voice& upper = voiceNumbered(bar, 1);
    REQUIRE(upper.events.size() == 4);
    CHECK(upper.events[0].onset == 0);
    CHECK(upper.events[1].onset == quarter);
    CHECK(upper.events[2].onset == quarter * 2);
    CHECK(upper.events[3].onset == quarter * 3);

    const Voice& lower = voiceNumbered(bar, 2);
    REQUIRE(lower.events.size() == 2);
    CHECK(lower.events[0].onset == 0);
    CHECK(lower.events[0].duration == half);
    CHECK(lower.events[1].onset == half);
}

TEST_CASE("four voices each start at the beginning of the measure", "[score][musicxml][timing]")
{
    std::string body = attributes(1, 4, 4);

    for (int voice = 1; voice <= 4; ++voice)
    {
        if (voice > 1)
        {
            body += backup(4);
        }

        body += note("C", 3 + voice, 4, voice);
    }

    const auto score = importOrFail(scoreDocument(measure("1", body)));
    const Measure& bar = score->parts.front().measures.front();

    REQUIRE(bar.voices.size() == 4);

    for (int voice = 1; voice <= 4; ++voice)
    {
        const Voice& part = voiceNumbered(bar, voice);

        REQUIRE(part.events.size() == 1);
        CHECK(part.events.front().onset == 0);
        CHECK(part.events.front().duration == whole);
    }
}

TEST_CASE("a backup to a non-zero position writes from that position", "[score][musicxml][timing]")
{
    // Write a half note, then back up only one quarter. The next note belongs
    // on beat 2, not on beat 1 and not on beat 3.
    const std::string body =
        measure("1", attributes(1, 4, 4) + note("C", 5, 2) + backup(1) + note("A", 4, 1, 2));

    const auto score = importOrFail(scoreDocument(body));
    const Measure& bar = score->parts.front().measures.front();

    const Voice& second = voiceNumbered(bar, 2);
    REQUIRE(second.events.size() == 1);
    CHECK(second.events.front().onset == quarter);
}

TEST_CASE(
    "a backup past the start of the measure is clamped and reported",
    "[score][musicxml][timing]")
{
    const std::string body =
        measure("1", attributes(1, 4, 4) + note("C", 5, 1) + backup(8) + note("A", 4, 1, 2));

    const MusicXmlImportResult result = importMusicXmlDocument(scoreDocument(body));

    REQUIRE(succeeded(result.status));
    REQUIRE(result.score != nullptr);

    const Measure& bar = result.score->parts.front().measures.front();
    CHECK(voiceNumbered(bar, 2).events.front().onset == 0);

    const auto reported = std::any_of(
        result.diagnostics.begin(), result.diagnostics.end(),
        [](const Diagnostic& diagnostic) { return diagnostic.elementName == "backup"; });
    CHECK(reported);
}

TEST_CASE("a forward skips the cursor ahead without writing anything", "[score][musicxml][timing]")
{
    // A forward past the end of the measure puts a note outside the bar.
    // Invariant 7 truncates it rather than letting it hang over the barline.
    const std::string body =
        measure("1", attributes(1, 4, 4) + note("C", 5, 1) + forward(2) + note("E", 5, 1));

    const auto score = importOrFail(scoreDocument(body));
    const Voice& voice = score->parts.front().measures.front().voices.front();

    REQUIRE(voice.events.size() == 2);
    CHECK(voice.events[0].onset == 0);

    // One quarter written, two quarters skipped: beat 4.
    CHECK(voice.events[1].onset == quarter * 3);
}

TEST_CASE("an event pushed past the end of the measure widens the bar", "[score][musicxml][timing]")
{
    const std::string body =
        measure("1", attributes(1, 4, 4) + note("C", 5, 4) + forward(4) + note("E", 5, 4));

    const MusicXmlImportResult result = importMusicXmlDocument(scoreDocument(body));

    REQUIRE(succeeded(result.status));

    const Measure& bar = result.score->parts.front().measures.front();

    // Two whole notes with a whole-note gap between them: three bars' worth of
    // time written into one bar. The bar grows to hold it rather than the
    // second note being thrown away.
    REQUIRE(bar.voices.front().events.size() == 2);
    CHECK(bar.nominalDuration == whole * 3);
    CHECK(bar.voices.front().events[1].onset == whole * 2);
    CHECK(bar.voices.front().events[1].duration == whole);

    CHECK_FALSE(result.diagnostics.empty());
}

TEST_CASE(
    "a mid-score change of divisions rescales into the same time base",
    "[score][musicxml][timing]")
{
    // Bar 1 counts in units of 1 per quarter, bar 2 in units of 24. A quarter
    // note is a quarter note in both, and both must land on the same tick
    // count -- that is the entire point of rescaling into one fixed base.
    const std::string body =
        measure("1", attributes(1, 4, 4) + note("C", 5, 4)) +
        measure(
            "2", "      <attributes><divisions>24</divisions></attributes>\n" + note("D", 5, 96));

    const auto score = importOrFail(scoreDocument(body));
    const Part& part = score->parts.front();

    REQUIRE(part.measures.size() == 2);
    CHECK(part.measures[0].voices.front().events.front().duration == whole);
    CHECK(part.measures[1].voices.front().events.front().duration == whole);

    // Both declarations are recorded so a diagnostic can quote what the file
    // actually said.
    REQUIRE(part.sourceDivisions.size() == 2);
    CHECK(part.sourceDivisions[0].divisions == 1);
    CHECK(part.sourceDivisions[1].divisions == 24);
}

TEST_CASE(
    "a divisions value that does not divide the time base is reported",
    "[score][musicxml][timing]")
{
    // 3840 ticks per quarter divides by 2, 3, and 5 but not by 7. A single
    // seventh of a quarter note cannot convert exactly, so it rounds and says
    // so rather than drifting silently.
    const std::string body = measure("1", attributes(7, 4, 4) + note("C", 5, 1) + note("D", 5, 27));

    const MusicXmlImportResult result = importMusicXmlDocument(scoreDocument(body));

    REQUIRE(succeeded(result.status));

    const auto rounded = std::any_of(
        result.diagnostics.begin(), result.diagnostics.end(),
        [](const Diagnostic& diagnostic) { return diagnostic.elementName == "duration"; });
    CHECK(rounded);
}

TEST_CASE("a pickup measure may hold less than its time signature", "[score][musicxml][timing]")
{
    const std::string body =
        measure("0", attributes(1, 4, 4) + note("G", 4, 1), /*implicit=*/true) +
        measure("1", note("C", 5, 4));

    const MusicXmlImportResult result = importMusicXmlDocument(scoreDocument(body));

    REQUIRE(succeeded(result.status));

    const Part& part = result.score->parts.front();
    REQUIRE(part.measures.size() == 2);

    const Measure& pickup = part.measures.front();
    CHECK(pickup.isPickup);
    CHECK(pickup.printedNumber == "0");
    CHECK(pickup.voices.front().events.front().duration == quarter);

    // Under-full, and exempt from invariant 7 because it is a pickup -- so no
    // repair is reported for it.
    const auto complained = std::any_of(
        result.diagnostics.begin(), result.diagnostics.end(),
        [](const Diagnostic& diagnostic) { return diagnostic.location.measureNumber == "0"; });
    CHECK_FALSE(complained);
}

TEST_CASE(
    "an over-full measure widens rather than losing its last note",
    "[score][musicxml][timing]")
{
    // Five quarter notes in a 4/4 bar. Real editions do this: a Renaissance
    // score may carry cut time as a mensuration sign while every bar holds a
    // breve, and truncating there silently halves every note in the piece.
    //
    // So an importer never destroys notes to satisfy a number it inferred. The
    // time signature is what the engraver wrote at the top of the staff; the
    // notes are the music. Where they disagree the notes win, and the
    // disagreement is reported.
    const std::string body = measure(
        "1", attributes(1, 4, 4) + note("C", 5, 1) + note("D", 5, 1) + note("E", 5, 1) +
                 note("F", 5, 1) + note("G", 5, 1));

    const MusicXmlImportResult result = importMusicXmlDocument(scoreDocument(body));

    REQUIRE(succeeded(result.status));

    const Measure& bar = result.score->parts.front().measures.front();

    // All five survive, in order, at their written positions.
    REQUIRE(bar.voices.front().events.size() == 5);

    for (std::size_t index = 0; index < 5; ++index)
    {
        CHECK(bar.voices.front().events[index].onset == quarter * static_cast<Tick>(index));
        CHECK(bar.voices.front().events[index].duration == quarter);
    }

    CHECK(bar.nominalDuration == quarter * 5);

    const auto reported = std::any_of(
        result.diagnostics.begin(), result.diagnostics.end(),
        [](const Diagnostic& diagnostic) { return diagnostic.elementName == "measure"; });
    CHECK(reported);
}

TEST_CASE("an under-full measure that is not a pickup is left alone", "[score][musicxml][timing]")
{
    // Invariant 7 bounds a voice from above, not from below. A bar holding less
    // than its time signature is wrong on paper but harmless in the model, and
    // padding it would invent music the file did not contain.
    const std::string body = measure("1", attributes(1, 4, 4) + note("C", 5, 2));

    const MusicXmlImportResult result = importMusicXmlDocument(scoreDocument(body));

    REQUIRE(succeeded(result.status));

    const Measure& bar = result.score->parts.front().measures.front();
    CHECK(bar.nominalDuration == whole);
    REQUIRE(bar.voices.front().events.size() == 1);
    CHECK(bar.voices.front().events.front().duration == half);
}

TEST_CASE("measures start where the previous one ended", "[score][musicxml][timing]")
{
    const std::string body = measure("1", attributes(1, 4, 4) + note("C", 5, 4)) +
                             measure("2", note("D", 5, 4)) + measure("3", note("E", 5, 4));

    const auto score = importOrFail(scoreDocument(body));
    const Part& part = score->parts.front();

    REQUIRE(part.measures.size() == 3);
    CHECK(part.measures[0].start == 0);
    CHECK(part.measures[1].start == whole);
    CHECK(part.measures[2].start == whole * 2);
    CHECK(totalLength(*score) == whole * 3);
}

TEST_CASE("parts with different measure counts are padded to agree", "[score][musicxml][timing]")
{
    // Invariant 1: measure n starts at the same tick in every part. A file
    // where one part stops early is reconciled rather than left ragged, because
    // every consumer indexes measures across parts.
    const std::string longer = measure("1", attributes(1, 4, 4) + note("C", 5, 4)) +
                               measure("2", note("D", 5, 4)) + measure("3", note("E", 5, 4));
    const std::string shorter = measure("1", attributes(1, 4, 4) + note("C", 3, 4));

    const MusicXmlImportResult result =
        importMusicXmlDocument(multiPartDocument({longer, shorter}));

    REQUIRE(succeeded(result.status));
    REQUIRE(result.score->parts.size() == 2);
    CHECK(result.score->parts[0].measures.size() == 3);
    CHECK(result.score->parts[1].measures.size() == 3);

    for (std::size_t index = 0; index < 3; ++index)
    {
        CHECK(
            result.score->parts[0].measures[index].start ==
            result.score->parts[1].measures[index].start);
    }

    CHECK_FALSE(result.diagnostics.empty());
}
