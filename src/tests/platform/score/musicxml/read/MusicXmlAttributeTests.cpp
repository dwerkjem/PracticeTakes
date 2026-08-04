#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <string>

#include "platform/score/musicxml/MusicXmlImporter.h"
#include "support/MusicXmlFixtures.h"

// Clefs, keys, time signatures, tempo, and dynamics -- including the ones that
// change partway through, which is where an importer that only reads the head
// of the file gets caught.
using Catch::Approx;
using namespace score;
using namespace score::musicxml;
using namespace testing::musicxml;

namespace
{
constexpr Tick quarter = ticksPerQuarterNote;

std::shared_ptr<const Score> importOrFail(const std::string& document)
{
    const MusicXmlImportResult result = importMusicXmlDocument(document);

    REQUIRE(succeeded(result.status));
    REQUIRE(result.score != nullptr);

    return result.score;
}

const Direction* directionOfKind(const Measure& measure, DirectionKind kind)
{
    for (const Direction& direction : measure.directions)
    {
        if (direction.kind == kind)
        {
            return &direction;
        }
    }

    return nullptr;
}

std::string tempoDirection(const std::string& inner)
{
    return "      <direction placement=\"above\">\n        <direction-type>\n" + inner +
           "        </direction-type>\n      </direction>\n";
}
} // namespace

TEST_CASE("clef, key, and time are read from the first measure", "[score][musicxml][attributes]")
{
    const std::string attributesNode =
        "      <attributes>\n"
        "        <divisions>1</divisions>\n"
        "        <key><fifths>-2</fifths><mode>major</mode></key>\n"
        "        <time><beats>3</beats><beat-type>4</beat-type></time>\n"
        "        <clef><sign>F</sign><line>4</line></clef>\n"
        "      </attributes>\n";

    const auto score = importOrFail(scoreDocument(measure("1", attributesNode + note("C", 3, 3))));
    const Measure& bar = score->parts.front().measures.front();

    REQUIRE(bar.attributes.key.has_value());
    CHECK(bar.attributes.key->fifths == -2);
    CHECK(bar.attributes.key->mode == "major");

    REQUIRE(bar.attributes.time.has_value());
    CHECK(bar.attributes.time->beats == 3);
    CHECK(bar.attributes.time->beatType == 4);
    CHECK(bar.nominalDuration == quarter * 3);

    REQUIRE(bar.attributes.clefs.size() == 1);
    CHECK(bar.attributes.clefs.front().sign == "F");
    CHECK(bar.attributes.clefs.front().line == 4);
}

TEST_CASE(
    "a mid-score clef change is attached to the measure it happens in",
    "[score][musicxml][attributes]")
{
    const std::string change =
        "      <attributes><clef><sign>C</sign><line>3</line></clef></attributes>\n";
    const std::string body = measure("1", attributes(1, 4, 4) + note("C", 4, 4)) +
                             measure("2", change + note("E", 4, 4));

    const auto score = importOrFail(scoreDocument(body));
    const Part& part = score->parts.front();

    // Only the measure that changes carries the change. A measure with no
    // attributes inherits whatever was in force.
    CHECK(part.measures[0].attributes.clefs.front().sign == "G");
    REQUIRE(part.measures[1].attributes.clefs.size() == 1);
    CHECK(part.measures[1].attributes.clefs.front().sign == "C");
    CHECK(part.measures[1].attributes.clefs.front().line == 3);
}

TEST_CASE(
    "a mid-score key change is attached to the measure it happens in",
    "[score][musicxml][attributes]")
{
    const std::string change = "      <attributes><key><fifths>3</fifths></key></attributes>\n";
    const std::string body = measure("1", attributes(1, 4, 4) + note("C", 4, 4)) +
                             measure("2", change + note("E", 4, 4));

    const auto score = importOrFail(scoreDocument(body));
    const Part& part = score->parts.front();

    CHECK(part.measures[0].attributes.key->fifths == 0);
    REQUIRE(part.measures[1].attributes.key.has_value());
    CHECK(part.measures[1].attributes.key->fifths == 3);
}

TEST_CASE(
    "a mid-score time change alters what the measure should hold",
    "[score][musicxml][attributes]")
{
    const std::string change =
        "      <attributes><time><beats>3</beats><beat-type>4</beat-type></time></attributes>\n";
    const std::string body = measure("1", attributes(1, 4, 4) + note("C", 4, 4)) +
                             measure("2", change + note("E", 4, 3)) + measure("3", note("G", 4, 3));

    const auto score = importOrFail(scoreDocument(body));
    const Part& part = score->parts.front();

    CHECK(part.measures[0].nominalDuration == quarter * 4);
    CHECK(part.measures[1].nominalDuration == quarter * 3);

    // The new signature stays in force after the measure that declared it.
    CHECK(part.measures[2].nominalDuration == quarter * 3);
    CHECK_FALSE(part.measures[2].attributes.time.has_value());

    // Measure starts follow from the durations, not from a fixed bar length.
    CHECK(part.measures[1].start == quarter * 4);
    CHECK(part.measures[2].start == quarter * 7);
}

TEST_CASE("tempo is read from a sound element", "[score][musicxml][attributes]")
{
    const std::string body =
        measure("1", attributes(1, 4, 4) + "      <sound tempo=\"144\"/>\n" + note("C", 5, 4));

    const auto score = importOrFail(scoreDocument(body));

    REQUIRE(score->tempoMap.entries().size() == 1);
    CHECK(score->tempoMap.entries().front().beatsPerMinute == Approx(144.0));
}

TEST_CASE(
    "tempo is read from a metronome marking when there is no sound element",
    "[score][musicxml][attributes]")
{
    // "half note = 60" is 120 quarter notes per minute. Reading the number
    // without the beat unit would halve the tempo.
    const std::string marking = tempoDirection(
        "          "
        "<metronome><beat-unit>half</beat-unit><per-minute>60</per-minute></metronome>\n");
    const std::string body = measure("1", attributes(1, 4, 4) + marking + note("C", 5, 4));

    const auto score = importOrFail(scoreDocument(body));

    REQUIRE(score->tempoMap.entries().size() == 1);
    CHECK(score->tempoMap.entries().front().beatsPerMinute == Approx(120.0));
}

TEST_CASE(
    "a dotted metronome beat unit is worth one and a half of itself",
    "[score][musicxml][attributes]")
{
    // "dotted quarter = 60" is 90 quarter notes per minute -- the natural
    // marking for 6/8, so getting it wrong misreads a whole class of music.
    const std::string marking =
        tempoDirection("          <metronome><beat-unit>quarter</beat-unit><beat-unit-dot/>"
                       "<per-minute>60</per-minute></metronome>\n");
    const std::string body = measure("1", attributes(1, 6, 8) + marking + note("C", 5, 3));

    const auto score = importOrFail(scoreDocument(body));

    REQUIRE(score->tempoMap.entries().size() == 1);
    CHECK(score->tempoMap.entries().front().beatsPerMinute == Approx(90.0));
}

TEST_CASE(
    "when the metronome mark and the playback tempo disagree, playback wins",
    "[score][musicxml][attributes]")
{
    // <sound tempo> is the explicit playback value; <metronome> is what is
    // engraved, which an exporter may leave stale or set for appearance. The
    // rule is stated in the reader and asserted here so it cannot drift.
    const std::string conflicting =
        "      <direction placement=\"above\">\n"
        "        <direction-type>\n"
        "          "
        "<metronome><beat-unit>quarter</beat-unit><per-minute>60</per-minute></metronome>\n"
        "        </direction-type>\n"
        "        <sound tempo=\"132\"/>\n"
        "      </direction>\n";
    const std::string body = measure("1", attributes(1, 4, 4) + conflicting + note("C", 5, 4));

    const MusicXmlImportResult result = importMusicXmlDocument(scoreDocument(body));

    REQUIRE(succeeded(result.status));
    REQUIRE(result.score->tempoMap.entries().size() == 1);
    CHECK(result.score->tempoMap.entries().front().beatsPerMinute == Approx(132.0));

    // Silently preferring one over the other would leave a user staring at a
    // metronome mark the application disagrees with, so the disagreement is
    // reported.
    const auto reported = std::any_of(
        result.diagnostics.begin(), result.diagnostics.end(),
        [](const Diagnostic& diagnostic) { return diagnostic.elementName == "metronome"; });
    CHECK(reported);
}

TEST_CASE("a mid-score tempo change lands at its own position", "[score][musicxml][attributes]")
{
    const std::string body =
        measure("1", attributes(1, 4, 4) + "      <sound tempo=\"60\"/>\n" + note("C", 5, 4)) +
        measure("2", "      <sound tempo=\"120\"/>\n" + note("D", 5, 4));

    const auto score = importOrFail(scoreDocument(body));

    REQUIRE(score->tempoMap.entries().size() == 2);
    CHECK(score->tempoMap.entries()[0].tick == 0);
    CHECK(score->tempoMap.entries()[0].beatsPerMinute == Approx(60.0));
    CHECK(score->tempoMap.entries()[1].tick == quarter * 4);
    CHECK(score->tempoMap.entries()[1].beatsPerMinute == Approx(120.0));

    // One bar at 60 is four seconds; the second bar at 120 is two more.
    CHECK(score->tempoMap.tickToSeconds(quarter * 4) == Approx(4.0));
    CHECK(score->tempoMap.tickToSeconds(quarter * 8) == Approx(6.0));
}

TEST_CASE(
    "a score that declares no tempo gets the documented default",
    "[score][musicxml][attributes]")
{
    const auto score =
        importOrFail(scoreDocument(measure("1", attributes(1, 4, 4) + note("C", 5, 4))));

    // Invariant 8: the map is never empty, so no consumer has to handle "no
    // tempo".
    REQUIRE(score->tempoMap.entries().size() == 1);
    CHECK(score->tempoMap.entries().front().tick == 0);
    CHECK(score->tempoMap.entries().front().beatsPerMinute == Approx(defaultBeatsPerMinute));
}

TEST_CASE(
    "a dynamic marking is attached to its part, measure, and position",
    "[score][musicxml][attributes]")
{
    const std::string dynamic =
        "      <direction placement=\"below\">\n"
        "        <direction-type><dynamics><mf/></dynamics></direction-type>\n"
        "      </direction>\n";
    const std::string body =
        measure("1", attributes(1, 4, 4) + note("C", 5, 2) + dynamic + note("D", 5, 2));

    const auto score = importOrFail(scoreDocument(body));
    const Measure& bar = score->parts.front().measures.front();

    const Direction* marking = directionOfKind(bar, DirectionKind::dynamic);
    REQUIRE(marking != nullptr);
    CHECK(marking->text == "mf");

    // Written after the first half note, so it belongs on beat 3 -- not at the
    // start of the bar, which is where an importer ignoring the cursor puts it.
    CHECK(marking->onset == quarter * 2);

    // A dynamic is notation attached to a position, not a playback velocity.
    // Turning "mf" into a gain is playback's job, and doing it here would bake
    // one interpretation into a model four consumers share.
    CHECK_FALSE(marking->beatsPerMinute.has_value());
}

TEST_CASE("score metadata is read from the head of the file", "[score][musicxml][attributes]")
{
    const std::string head = R"(<?xml version="1.0" encoding="UTF-8"?>
<score-partwise version="4.0">
  <work><work-title>Ave Verum Corpus</work-title></work>
  <movement-title>Andante</movement-title>
  <identification>
    <creator type="composer">W. A. Mozart</creator>
    <creator type="lyricist">Anonymous</creator>
    <encoding><software>MuseScore 4.4.2</software></encoding>
  </identification>
  <part-list><score-part id="P1"><part-name>Soprano</part-name>
    <part-abbreviation>S.</part-abbreviation></score-part></part-list>
  <part id="P1">
)" + measure("1", attributes(1, 4, 4) + note("C", 5, 4)) +
                             R"(  </part>
</score-partwise>
)";

    const auto score = importOrFail(head);

    CHECK(score->metadata.workTitle == "Ave Verum Corpus");
    CHECK(score->metadata.movementTitle == "Andante");
    CHECK(score->metadata.composer == "W. A. Mozart");
    CHECK(score->metadata.lyricist == "Anonymous");

    // Worth keeping: exporter dialects differ more than the format suggests, so
    // this is the first thing anyone wants when a file misbehaves.
    CHECK(score->metadata.encodingSoftware == "MuseScore 4.4.2");

    CHECK(score->parts.front().name == "Soprano");
    CHECK(score->parts.front().abbreviation == "S.");
    CHECK(score->parts.front().id == "P1");
}

TEST_CASE(
    "repeat barlines and endings are captured but not expanded",
    "[score][musicxml][attributes]")
{
    // Decision 3: the model is as-written. Measures appear once, in source
    // order, and the repeat data is kept faithfully so that whichever change
    // first needs to *hear* a repeat can derive a playback order from it.
    const std::string openRepeat =
        "      <barline location=\"left\"><repeat direction=\"forward\"/></barline>\n";
    const std::string closeRepeat =
        "      <barline location=\"right\">"
        "<ending number=\"1, 2\" type=\"stop\"/>"
        "<repeat direction=\"backward\" times=\"3\"/></barline>\n";
    const std::string body = measure("1", attributes(1, 4, 4) + openRepeat + note("C", 5, 4)) +
                             measure("2", note("D", 5, 4) + closeRepeat);

    const auto score = importOrFail(scoreDocument(body));
    const Part& part = score->parts.front();

    // Two measures in, two measures out. Nothing was expanded.
    REQUIRE(part.measures.size() == 2);

    CHECK(part.measures[0].repeats.repeatStart);
    CHECK_FALSE(part.measures[0].repeats.repeatEnd);

    CHECK(part.measures[1].repeats.repeatEnd);
    CHECK(part.measures[1].repeats.repeatTimes == 3);
    REQUIRE(part.measures[1].repeats.endingNumbers.size() == 2);
    CHECK(part.measures[1].repeats.endingNumbers[0] == 1);
    CHECK(part.measures[1].repeats.endingNumbers[1] == 2);
}
