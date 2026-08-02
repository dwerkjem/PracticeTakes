#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <string>

#include "platform/score/musicxml/MusicXmlImporter.h"
#include "support/MusicXmlFixtures.h"

// Pitch, chords, ties, rests, and grace notes.
//
// The theme running through this file is the model's insistence on keeping both
// halves of a pitch. A C-sharp and a D-flat are the same sounding number and
// different staff positions, so a score that stored only the number could not
// be engraved and one that stored only the spelling would make every playback
// consumer redo the arithmetic. Several cases here exist only to hold that line.
using namespace score;
using namespace score::musicxml;
using namespace testing::musicxml;

namespace
{
constexpr Tick quarter = ticksPerQuarterNote;
constexpr Tick whole = quarter * 4;

std::shared_ptr<const Score> importOrFail(const std::string& document)
{
    const MusicXmlImportResult result = importMusicXmlDocument(document);

    REQUIRE(succeeded(result.status));
    REQUIRE(result.score != nullptr);

    return result.score;
}

// A <note> with an explicit <alter>, which the plain fixture helper omits.
std::string alteredNote(const std::string& step, int alter, int octave, int duration)
{
    return "      <note>\n"
           "        <pitch><step>" +
           step + "</step><alter>" + std::to_string(alter) + "</alter><octave>" +
           std::to_string(octave) +
           "</octave></pitch>\n"
           "        <duration>" +
           std::to_string(duration) +
           "</duration>\n"
           "        <voice>1</voice>\n"
           "      </note>\n";
}

const ScoreEvent& firstEvent(const Score& score, std::size_t measureIndex = 0)
{
    return score.parts.front().measures[measureIndex].voices.front().events.front();
}
} // namespace

TEST_CASE(
    "enharmonic spellings keep both the spelling and the sounding pitch",
    "[score][musicxml][note]")
{
    // C-sharp and D-flat are the same key on a piano and different notes on a
    // page. Both facts have to survive the import.
    const std::string body =
        measure("1", attributes(1, 4, 4) + alteredNote("C", 1, 4, 2) + alteredNote("D", -1, 4, 2));

    const auto score = importOrFail(scoreDocument(body));
    const Voice& voice = score->parts.front().measures.front().voices.front();

    REQUIRE(voice.events.size() == 2);

    const Pitch& sharp = voice.events[0].pitches.front();
    CHECK(sharp.step == Step::c);
    CHECK(sharp.alter == 1);
    CHECK(sharp.octave == 4);
    CHECK(sharp.midiNoteNumber == 61);

    const Pitch& flat = voice.events[1].pitches.front();
    CHECK(flat.step == Step::d);
    CHECK(flat.alter == -1);
    CHECK(flat.midiNoteNumber == 61);

    // Same sound, different spelling -- which is exactly the pair of facts a
    // model holding only one of them would lose.
    CHECK(soundsSameAs(sharp, flat));
    CHECK(sharp != flat);
    CHECK(isConsistent(sharp));
    CHECK(isConsistent(flat));
}

TEST_CASE("a three-note chord is one event consuming one duration", "[score][musicxml][note]")
{
    const std::string chordExtra = "        <chord/>\n";
    const std::string body = measure(
        "1", attributes(1, 4, 4) + note("C", 4, 4) + note("E", 4, 4, 1, chordExtra) +
                 note("G", 4, 4, 1, chordExtra));

    const auto score = importOrFail(scoreDocument(body));
    const Voice& voice = score->parts.front().measures.front().voices.front();

    // Three <note> elements, one event. Invariant 2's "no two events overlap"
    // is only a meaningful rule because of this.
    REQUIRE(voice.events.size() == 1);

    const ScoreEvent& chord = voice.events.front();
    CHECK(chord.kind == EventKind::chord);
    CHECK(chord.duration == whole);
    REQUIRE(chord.pitches.size() == 3);
    CHECK(chord.pitches[0].midiNoteNumber == 60);
    CHECK(chord.pitches[1].midiNoteNumber == 64);
    CHECK(chord.pitches[2].midiNoteNumber == 67);
}

TEST_CASE("a tie across a barline links both events", "[score][musicxml][note]")
{
    const std::string body =
        measure("1", attributes(1, 4, 4) + note("C", 5, 4, 1, "        <tie type=\"start\"/>\n")) +
        measure("2", note("C", 5, 4, 1, "        <tie type=\"stop\"/>\n"));

    const MusicXmlImportResult result = importMusicXmlDocument(scoreDocument(body));

    REQUIRE(succeeded(result.status));

    const Part& part = result.score->parts.front();
    const ScoreEvent& start = part.measures[0].voices.front().events.front();
    const ScoreEvent& stop = part.measures[1].voices.front().events.front();

    REQUIRE(start.tiedTo.has_value());
    REQUIRE(stop.tiedFrom.has_value());

    // Both ends point at each other. A half-linked tie is a null dereference
    // waiting to happen in every consumer, so invariant 4 requires the mirror.
    CHECK(start.tiedTo->measureIndex == 1);
    CHECK(start.tiedTo->eventIndex == 0);
    CHECK(stop.tiedFrom->measureIndex == 0);
    CHECK(stop.tiedFrom->eventIndex == 0);
}

TEST_CASE("a tie that never ends is dropped with a diagnostic", "[score][musicxml][note]")
{
    const std::string body =
        measure("1", attributes(1, 4, 4) + note("C", 5, 4, 1, "        <tie type=\"start\"/>\n")) +
        measure("2", note("D", 5, 4));

    const MusicXmlImportResult result = importMusicXmlDocument(scoreDocument(body));

    REQUIRE(succeeded(result.status));

    const ScoreEvent& dangling =
        result.score->parts.front().measures[0].voices.front().events.front();
    CHECK_FALSE(dangling.tiedTo.has_value());

    const auto reported = std::any_of(
        result.diagnostics.begin(), result.diagnostics.end(),
        [](const Diagnostic& diagnostic) { return diagnostic.elementName == "tie"; });
    CHECK(reported);
}

TEST_CASE(
    "a tie that ends without starting is dropped with a diagnostic",
    "[score][musicxml][note]")
{
    const std::string body = measure("1", attributes(1, 4, 4) + note("C", 5, 4)) +
                             measure("2", note("C", 5, 4, 1, "        <tie type=\"stop\"/>\n"));

    const MusicXmlImportResult result = importMusicXmlDocument(scoreDocument(body));

    REQUIRE(succeeded(result.status));

    const ScoreEvent& orphan =
        result.score->parts.front().measures[1].voices.front().events.front();
    CHECK_FALSE(orphan.tiedFrom.has_value());
    CHECK_FALSE(result.diagnostics.empty());
}

TEST_CASE("a slur is not a tie", "[score][musicxml][note]")
{
    // <slur> is a phrasing mark, <tie> is a sounding link, and <notations><tied>
    // is the tie's engraved counterpart. All three look alike on the page and
    // are routinely confused. A slur must produce no tie link at all.
    const std::string slurStart =
        "        <notations><slur type=\"start\" number=\"1\"/></notations>\n";
    const std::string slurStop =
        "        <notations><slur type=\"stop\" number=\"1\"/></notations>\n";
    const std::string body = measure(
        "1", attributes(1, 4, 4) + note("C", 5, 2, 1, slurStart) + note("D", 5, 2, 1, slurStop));

    const MusicXmlImportResult result = importMusicXmlDocument(scoreDocument(body));

    REQUIRE(succeeded(result.status));

    const Voice& voice = result.score->parts.front().measures.front().voices.front();
    REQUIRE(voice.events.size() == 2);

    for (const ScoreEvent& event : voice.events)
    {
        CHECK_FALSE(event.tiedTo.has_value());
        CHECK_FALSE(event.tiedFrom.has_value());
    }

    // Dropped, and said so -- a slur is in the recognised-but-unsupported list,
    // not the unrecognised one.
    const auto reported = std::any_of(
        result.diagnostics.begin(), result.diagnostics.end(),
        [](const Diagnostic& diagnostic)
        {
            return diagnostic.elementName == "slur" &&
                   diagnostic.severity == DiagnosticSeverity::unsupported;
        });
    CHECK(reported);
}

TEST_CASE("a whole-measure rest lasts the measure", "[score][musicxml][note]")
{
    // <rest measure="yes"> lasts the bar whatever the bar is, and exporters
    // routinely omit its <duration> entirely.
    const std::string wholeRest =
        "      <note>\n"
        "        <rest measure=\"yes\"/>\n"
        "        <voice>1</voice>\n"
        "      </note>\n";
    const std::string body = measure("1", attributes(1, 3, 4) + wholeRest);

    const auto score = importOrFail(scoreDocument(body));
    const Measure& bar = score->parts.front().measures.front();

    // 3/4, so three quarters -- not four.
    CHECK(bar.nominalDuration == quarter * 3);

    const ScoreEvent& rest = bar.voices.front().events.front();
    CHECK(rest.kind == EventKind::rest);
    CHECK(rest.duration == quarter * 3);
    CHECK(rest.pitches.empty());
}

TEST_CASE("a grace note takes no time and does not move the cursor", "[score][musicxml][note]")
{
    const std::string grace =
        "      <note>\n"
        "        <grace/>\n"
        "        <pitch><step>B</step><octave>4</octave></pitch>\n"
        "        <voice>1</voice>\n"
        "      </note>\n";
    const std::string body =
        measure("1", attributes(1, 4, 4) + note("C", 5, 2) + grace + note("D", 5, 2));

    const auto score = importOrFail(scoreDocument(body));
    const Voice& voice = score->parts.front().measures.front().voices.front();

    REQUIRE(voice.events.size() == 3);

    CHECK(voice.events[0].onset == 0);
    CHECK(voice.events[0].duration == quarter * 2);

    // The grace note sits on the beat its principal note starts on and consumes
    // none of it, so the note after it still lands on beat 3.
    CHECK(voice.events[1].isGrace);
    CHECK(voice.events[1].duration == 0);
    CHECK(voice.events[1].onset == quarter * 2);

    CHECK_FALSE(voice.events[2].isGrace);
    CHECK(voice.events[2].onset == quarter * 2);
}

TEST_CASE("a tuplet keeps its ratio but not its bracket", "[score][musicxml][note]")
{
    // The ratio makes the duration arithmetic add up; the bracket a program
    // draws is engraving data that invariant 10 keeps out of the model.
    const std::string triplet = "        <time-modification><actual-notes>3</actual-notes>"
                                "<normal-notes>2</normal-notes></time-modification>\n";
    const std::string body = measure(
        "1", attributes(6, 4, 4) + note("C", 5, 2, 1, triplet) + note("D", 5, 2, 1, triplet) +
                 note("E", 5, 2, 1, triplet));

    const auto score = importOrFail(scoreDocument(body));
    const Voice& voice = score->parts.front().measures.front().voices.front();

    REQUIRE(voice.events.size() == 3);

    for (const ScoreEvent& event : voice.events)
    {
        CHECK(event.tuplet.actual == 3);
        CHECK(event.tuplet.normal == 2);
        CHECK_FALSE(isPlainTuplet(event.tuplet));
    }
}

TEST_CASE("a note on a second staff records which staff it is on", "[score][musicxml][note]")
{
    const std::string onStaffTwo =
        "      <note>\n"
        "        <pitch><step>C</step><octave>3</octave></pitch>\n"
        "        <duration>4</duration>\n"
        "        <voice>2</voice>\n"
        "        <staff>2</staff>\n"
        "      </note>\n";
    const std::string body = measure(
        "1", "      <attributes><divisions>1</divisions><staves>2</staves>"
             "<time><beats>4</beats><beat-type>4</beat-type></time></attributes>\n" +
                 note("C", 5, 4) + backup(4) + onStaffTwo);

    const auto score = importOrFail(scoreDocument(body));
    const Part& part = score->parts.front();

    CHECK(part.staffCount == 2);

    const Measure& bar = part.measures.front();
    REQUIRE(bar.voices.size() == 2);
    CHECK(bar.voices[0].events.front().staff == 1);
    CHECK(bar.voices[1].events.front().staff == 2);
}

TEST_CASE(
    "a note with an unreadable pitch becomes a rest rather than a wrong note",
    "[score][musicxml][note]")
{
    // Guessing at what "H" meant is how a wrong score gets imported quietly. A
    // rest is audibly and visibly absent, which is the honest failure.
    const std::string broken =
        "      <note>\n"
        "        <pitch><step>H</step><octave>4</octave></pitch>\n"
        "        <duration>4</duration>\n"
        "        <voice>1</voice>\n"
        "      </note>\n";

    const MusicXmlImportResult result =
        importMusicXmlDocument(scoreDocument(measure("1", attributes(1, 4, 4) + broken)));

    REQUIRE(succeeded(result.status));

    const ScoreEvent& event = firstEvent(*result.score);
    CHECK(event.kind == EventKind::rest);
    CHECK(event.pitches.empty());
    CHECK_FALSE(result.diagnostics.empty());
}
