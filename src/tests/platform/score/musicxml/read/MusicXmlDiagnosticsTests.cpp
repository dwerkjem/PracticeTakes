#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "platform/score/musicxml/MusicXmlImporter.h"
#include "support/MusicXmlFixtures.h"

// The subset boundary and what the importer says about it.
//
// The rule these cases exist to hold is task 7.3: **no amount of unsupported or
// unrecognised content can turn a successful import into a failure.** A score
// with a slur on every phrase and a thousand layout elements is still a score
// worth practising with, and an importer that refused it would be useless on
// real files. Everything below either asserts that a diagnostic was produced,
// or that producing it did not cost the score.
using namespace score;
using namespace score::musicxml;
using namespace testing::musicxml;

namespace
{
const Diagnostic* find(const std::vector<Diagnostic>& diagnostics, const std::string& elementName)
{
    for (const Diagnostic& diagnostic : diagnostics)
    {
        if (diagnostic.elementName == elementName)
        {
            return &diagnostic;
        }
    }

    return nullptr;
}

std::size_t countFor(const std::vector<Diagnostic>& diagnostics, const std::string& elementName)
{
    return static_cast<std::size_t>(std::count_if(
        diagnostics.begin(), diagnostics.end(), [&elementName](const Diagnostic& diagnostic)
        { return diagnostic.elementName == elementName; }));
}
} // namespace

TEST_CASE(
    "a per-measure problem names its part, measure, and voice",
    "[score][musicxml][diagnostics]")
{
    // A backup past the start of bar 2. The diagnostic has to name the bar a
    // user would look at on the page, not an index into a vector.
    const std::string body = measure("1", attributes(1, 4, 4) + note("C", 5, 4)) +
                             measure("7", note("D", 5, 1) + backup(9) + note("E", 4, 1, 2));

    const MusicXmlImportResult result = importMusicXmlDocument(scoreDocument(body));

    REQUIRE(succeeded(result.status));

    const Diagnostic* backupProblem = find(result.diagnostics, "backup");
    REQUIRE(backupProblem != nullptr);
    REQUIRE(backupProblem->location.partId.has_value());
    CHECK(*backupProblem->location.partId == "P1");
    REQUIRE(backupProblem->location.measureNumber.has_value());

    // "7" -- what the file printed, not "1" for the zero-based index.
    CHECK(*backupProblem->location.measureNumber == "7");
    CHECK(hasLocation(backupProblem->location));
}

TEST_CASE("a non-numeric measure number is quoted as printed", "[score][musicxml][diagnostics]")
{
    // MusicXML measure numbers are not integers: a pickup is "0" and a split
    // bar is "12a". Storing what was printed is what lets a diagnostic name the
    // bar the user sees.
    const std::string body = measure("1", attributes(1, 4, 4) + note("C", 5, 4)) +
                             measure("12a", note("D", 5, 1) + backup(9) + note("E", 4, 1, 2));

    const MusicXmlImportResult result = importMusicXmlDocument(scoreDocument(body));

    REQUIRE(succeeded(result.status));
    CHECK(result.score->parts.front().measures[1].printedNumber == "12a");

    const Diagnostic* backupProblem = find(result.diagnostics, "backup");
    REQUIRE(backupProblem != nullptr);
    REQUIRE(backupProblem->location.measureNumber.has_value());
    CHECK(*backupProblem->location.measureNumber == "12a");
}

TEST_CASE("a document-level problem carries no musical location", "[score][musicxml][diagnostics]")
{
    // An unrecognised element is summarised for the whole document, because the
    // useful report is "347 of these were ignored" rather than where each was.
    const std::string body =
        measure("1", attributes(1, 4, 4) + "      <invented-by-nobody/>\n" + note("C", 5, 4));

    const MusicXmlImportResult result = importMusicXmlDocument(scoreDocument(body));

    REQUIRE(succeeded(result.status));

    const Diagnostic* unknown = find(result.diagnostics, "invented-by-nobody");
    REQUIRE(unknown != nullptr);
    CHECK_FALSE(hasLocation(unknown->location));
    CHECK(unknown->severity == DiagnosticSeverity::info);
}

TEST_CASE(
    "a repeated unrecognised element is summarised once with a count",
    "[score][musicxml][diagnostics]")
{
    // A Sibelius export contains thousands of layout elements this importer has
    // no use for. One diagnostic per occurrence would bury every real finding
    // under them, so they are aggregated by name.
    std::string body = attributes(1, 4, 4);

    for (int occurrence = 0; occurrence < 25; ++occurrence)
    {
        body += "      <vendor-specific-thing/>\n";
    }

    body += note("C", 5, 4);

    const MusicXmlImportResult result = importMusicXmlDocument(scoreDocument(measure("1", body)));

    REQUIRE(succeeded(result.status));

    // Once, not twenty-five times.
    CHECK(countFor(result.diagnostics, "vendor-specific-thing") == 1);

    const Diagnostic* summary = find(result.diagnostics, "vendor-specific-thing");
    REQUIRE(summary != nullptr);
    CHECK(summary->occurrences == 25);
}

TEST_CASE(
    "a repeated unsupported construct is reported once with a count",
    "[score][musicxml][diagnostics]")
{
    // Same reasoning for the recognised-but-dropped list: a piano score has a
    // slur on nearly every phrase, and the message describes the construct
    // rather than the instance, so repeating it adds nothing a count does not.
    std::string body = attributes(1, 4, 4);

    for (int occurrence = 0; occurrence < 8; ++occurrence)
    {
        body += note("C", 5, 4, 1, "        <notations><slur type=\"start\"/></notations>\n");
    }

    const MusicXmlImportResult result = importMusicXmlDocument(scoreDocument(measure("1", body)));

    REQUIRE(succeeded(result.status));
    CHECK(countFor(result.diagnostics, "slur") == 1);

    const Diagnostic* slur = find(result.diagnostics, "slur");
    REQUIRE(slur != nullptr);
    CHECK(slur->occurrences == 8);
    CHECK(slur->severity == DiagnosticSeverity::unsupported);

    // The first sighting's location is kept, so the report still points
    // somewhere rather than nowhere.
    CHECK(hasLocation(slur->location));
}

TEST_CASE(
    "an import succeeds despite dropped and unrecognised content",
    "[score][musicxml][diagnostics]")
{
    // Task 7.3, stated as directly as it can be: a file stuffed with things
    // this importer does not read still produces a score.
    const std::string noisy =
        "      <print new-system=\"yes\"/>\n"
        "      <harmony><root><root-step>C</root-step></root></harmony>\n"
        "      <something-from-the-future/>\n" +
        note(
            "C", 5, 4, 1,
            "        <notations><slur type=\"start\"/><articulations><staccato/>"
            "</articulations><ornaments><trill-mark/></ornaments></notations>\n");

    const MusicXmlImportResult result =
        importMusicXmlDocument(scoreDocument(measure("1", attributes(1, 4, 4) + noisy)));

    CHECK(result.status == MusicXmlImportStatus::importedWithDiagnostics);
    REQUIRE(result.score != nullptr);

    // The music survived intact.
    const Voice& voice = result.score->parts.front().measures.front().voices.front();
    REQUIRE(voice.events.size() == 1);
    CHECK(voice.events.front().pitches.front().midiNoteNumber == 72);

    // And every category was reported.
    CHECK(find(result.diagnostics, "print") != nullptr);
    CHECK(find(result.diagnostics, "harmony") != nullptr);
    CHECK(find(result.diagnostics, "slur") != nullptr);
    CHECK(find(result.diagnostics, "articulations") != nullptr);
    CHECK(find(result.diagnostics, "ornaments") != nullptr);
    CHECK(find(result.diagnostics, "something-from-the-future") != nullptr);
}

TEST_CASE(
    "a transposing instrument is imported at written pitch and flagged",
    "[score][musicxml][diagnostics]")
{
    // A real gap, deliberately taken: a B-flat part will sound and display a
    // whole tone off. Acceptable for an MVP aimed at singers and piano,
    // unacceptable the moment anyone loads a band score -- so it says so rather
    // than being discovered later.
    const std::string transposing =
        "      <attributes>\n"
        "        <divisions>1</divisions>\n"
        "        <time><beats>4</beats><beat-type>4</beat-type></time>\n"
        "        <transpose><diatonic>-1</diatonic><chromatic>-2</chromatic></transpose>\n"
        "      </attributes>\n";

    const MusicXmlImportResult result =
        importMusicXmlDocument(scoreDocument(measure("1", transposing + note("D", 5, 4))));

    REQUIRE(succeeded(result.status));

    const Diagnostic* transpose = find(result.diagnostics, "transpose");
    REQUIRE(transpose != nullptr);
    CHECK(transpose->severity == DiagnosticSeverity::unsupported);

    // Written pitch, unchanged.
    const Voice& voice = result.score->parts.front().measures.front().voices.front();
    CHECK(voice.events.front().pitches.front().midiNoteNumber == 74);
}

TEST_CASE("a clean file produces no diagnostics at all", "[score][musicxml][diagnostics]")
{
    // The counterweight to everything above. If `imported` were unreachable in
    // practice, the distinction between it and `importedWithDiagnostics` would
    // be worthless, and a caller could never tell a clean file from a repaired
    // one.
    const std::string body = measure("1", attributes(1, 4, 4) + note("C", 5, 2) + note("D", 5, 2)) +
                             measure("2", note("E", 5, 4));

    const MusicXmlImportResult result = importMusicXmlDocument(scoreDocument(body));

    CHECK(result.status == MusicXmlImportStatus::imported);
    CHECK(result.diagnostics.empty());
    REQUIRE(result.score != nullptr);
    CHECK(result.score->diagnostics.empty());
}

TEST_CASE("a missing part identifier is generated and reported", "[score][musicxml][diagnostics]")
{
    // Invariant 6: part identifiers are unique and non-empty, because #39's
    // session file needs to name a selected part stably across sessions and two
    // parts sharing an id makes that impossible.
    const std::string document = R"(<?xml version="1.0"?>
<score-partwise version="4.0">
  <part-list>
    <score-part id="P1"><part-name>Soprano</part-name></score-part>
    <score-part id="P1"><part-name>Alto</part-name></score-part>
  </part-list>
  <part id="P1">
)" + measure("1", attributes(1, 4, 4) + note("C", 5, 4)) +
                                 R"(  </part>
</score-partwise>
)";

    const MusicXmlImportResult result = importMusicXmlDocument(document);

    REQUIRE(succeeded(result.status));
    REQUIRE(result.score->parts.size() == 2);

    CHECK(result.score->parts[0].id == "P1");
    CHECK(result.score->parts[1].id != "P1");
    CHECK_FALSE(result.score->parts[1].id.empty());

    CHECK(find(result.diagnostics, "score-part") != nullptr);
}

TEST_CASE(
    "a document with no music at all is a structural failure",
    "[score][musicxml][diagnostics]")
{
    // Task 7.5. Stated over *events* rather than over notes: a movement of
    // nothing but rests is unusual and entirely valid, and refusing it would
    // reject a real score to catch a hypothetical one. Zero events cannot be a
    // real score.
    const std::string empty = scoreDocument(measure("1", attributes(1, 4, 4)));

    const MusicXmlImportResult result = importMusicXmlDocument(empty);

    CHECK(result.status == MusicXmlImportStatus::structurallyInvalid);
    CHECK(result.score == nullptr);
    CHECK_FALSE(result.error.empty());
}

TEST_CASE("a document of nothing but rests still imports", "[score][musicxml][diagnostics]")
{
    // The other half of the rule above, and the reason it is stated over events.
    const std::string body = measure("1", attributes(1, 4, 4) + rest(4)) + measure("2", rest(4));

    const MusicXmlImportResult result = importMusicXmlDocument(scoreDocument(body));

    REQUIRE(succeeded(result.status));
    REQUIRE(result.score != nullptr);
    CHECK(measureCount(*result.score) == 2);
}

TEST_CASE(
    "a diagnostic never names a measure the score does not have",
    "[score][musicxml][diagnostics]")
{
    // Invariant 9. Whatever the importer reported, every location in the
    // finished score has to resolve -- a diagnostic pointing at a bar that was
    // dropped during repair is worse than no diagnostic.
    const std::string body =
        measure("1", attributes(1, 4, 4) + note("C", 5, 1) + backup(9) + note("E", 4, 1, 2)) +
        measure("2", note("D", 5, 9));

    const MusicXmlImportResult result = importMusicXmlDocument(scoreDocument(body));

    REQUIRE(succeeded(result.status));

    for (const Diagnostic& diagnostic : result.score->diagnostics)
    {
        if (!diagnostic.location.partId.has_value())
        {
            continue;
        }

        const auto part = std::find_if(
            result.score->parts.begin(), result.score->parts.end(),
            [&diagnostic](const Part& candidate)
            { return candidate.id == *diagnostic.location.partId; });
        REQUIRE(part != result.score->parts.end());

        if (!diagnostic.location.measureNumber.has_value())
        {
            continue;
        }

        const auto measureNamed = std::find_if(
            part->measures.begin(), part->measures.end(), [&diagnostic](const Measure& candidate)
            { return candidate.printedNumber == *diagnostic.location.measureNumber; });
        CHECK(measureNamed != part->measures.end());
    }
}
