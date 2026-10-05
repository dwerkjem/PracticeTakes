#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "application/shell/ui/score/ScoreImportSummary.h"

#include "platform/score/Score.h"

using Catch::Approx;
using score::Diagnostic;
using score::DiagnosticSeverity;
using score::Measure;
using score::Part;
using score::Score;
using score::TempoEntry;
using score::TempoMap;
using score::ticksPerQuarterNote;
using score::musicxml::MusicXmlImportResult;
using score::musicxml::MusicXmlImportStatus;

namespace
{
constexpr score::Tick barTicks = ticksPerQuarterNote * 4;

// `barCount` bars of 4/4 for one part. No events: the summary describes
// structure and metadata, so what is inside a measure is not its business.
[[nodiscard]] std::vector<Measure> barsOf4_4(const std::size_t barCount)
{
    std::vector<Measure> measures;
    measures.reserve(barCount);

    for (std::size_t index = 0; index < barCount; ++index)
    {
        Measure measure;
        measure.printedNumber = std::to_string(index + 1);
        measure.index = index;
        measure.start = static_cast<score::Tick>(index) * barTicks;
        measure.nominalDuration = barTicks;
        measures.push_back(measure);
    }

    return measures;
}

[[nodiscard]] Part
partOf(std::string id, std::string name, const int staffCount, const std::size_t barCount)
{
    Part part;
    part.id = std::move(id);
    part.name = std::move(name);
    part.staffCount = staffCount;
    part.measures = barsOf4_4(barCount);

    return part;
}

// A four-bar vocal score with full metadata, at the default tempo.
[[nodiscard]] Score makeVocalScore()
{
    Score score;
    score.metadata.workTitle = "Requiem";
    score.metadata.movementTitle = "Introitus";
    score.metadata.composer = "Fauré";
    score.metadata.lyricist = "Liturgical";
    score.metadata.encodingSoftware = "MuseScore 4.4.2";
    score.parts = {
        partOf("P1", "Soprano", 1, 4), partOf("P2", "Alto", 1, 4), partOf("P3", "Piano", 2, 4)};

    return score;
}

[[nodiscard]] MusicXmlImportResult
resultOf(Score score, const MusicXmlImportStatus status, std::vector<Diagnostic> diagnostics = {})
{
    MusicXmlImportResult result;
    result.status = status;
    result.score = std::make_shared<const Score>(std::move(score));
    result.diagnostics = std::move(diagnostics);

    return result;
}

[[nodiscard]] const ScoreImportField*
findField(const ScoreImportSummary& summary, const std::string& label)
{
    const auto found = std::find_if(
        summary.fields.begin(), summary.fields.end(),
        [&label](const ScoreImportField& field) { return field.label == label; });

    return found == summary.fields.end() ? nullptr : &*found;
}

[[nodiscard]] bool hasField(const ScoreImportSummary& summary, const std::string& label)
{
    return findField(summary, label) != nullptr;
}

[[nodiscard]] std::string valueOf(const ScoreImportSummary& summary, const std::string& label)
{
    const ScoreImportField* const field = findField(summary, label);

    return field == nullptr ? std::string{} : field->value;
}

[[nodiscard]] Diagnostic
diagnosticOf(const DiagnosticSeverity severity, std::string message, std::string elementName = {})
{
    Diagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.message = std::move(message);
    diagnostic.elementName = std::move(elementName);

    return diagnostic;
}

// Every status the importer can return, so the exhaustiveness test cannot be
// satisfied by a subset.
constexpr std::array<MusicXmlImportStatus, 10> everyStatus{
    MusicXmlImportStatus::imported,
    MusicXmlImportStatus::importedWithDiagnostics,
    MusicXmlImportStatus::notFound,
    MusicXmlImportStatus::unreadable,
    MusicXmlImportStatus::tooLarge,
    MusicXmlImportStatus::notMusicXml,
    MusicXmlImportStatus::malformedXml,
    MusicXmlImportStatus::invalidContainer,
    MusicXmlImportStatus::unsupportedDocumentType,
    MusicXmlImportStatus::structurallyInvalid};
} // namespace

TEST_CASE("a multi-part score names every part with its staff count", "[score][import][summary]")
{
    const ScoreImportSummary summary =
        summariseScoreImport(resultOf(makeVocalScore(), MusicXmlImportStatus::imported));

    REQUIRE(summary.succeeded);
    REQUIRE(summary.parts.size() == 3);

    CHECK(summary.parts[0].name == "Soprano");
    CHECK(summary.parts[0].staffCount == 1);
    CHECK(summary.parts[1].name == "Alto");
    CHECK(summary.parts[2].name == "Piano");

    // The piano's two staves are the case that makes staff count worth showing
    // at all: a part that imported with one staff is visibly wrong.
    CHECK(summary.parts[2].staffCount == 2);

    CHECK(valueOf(summary, "Parts") == "3");
    CHECK(summary.measureCount == 4);
    CHECK(valueOf(summary, "Measures") == "4");
}

TEST_CASE(
    "a score's metadata is reported, including its encoding software",
    "[score][import][summary]")
{
    const ScoreImportSummary summary =
        summariseScoreImport(resultOf(makeVocalScore(), MusicXmlImportStatus::imported));

    CHECK(valueOf(summary, "Work") == "Requiem");
    CHECK(valueOf(summary, "Movement") == "Introitus");
    CHECK(valueOf(summary, "Composer") == "Fauré");
    CHECK(valueOf(summary, "Lyricist") == "Liturgical");
    CHECK(valueOf(summary, "Written by") == "MuseScore 4.4.2");
}

TEST_CASE(
    "a score with no metadata omits those fields and still reports its structure",
    "[score][import][summary]")
{
    Score score;
    score.parts = {partOf("P1", "", 1, 2)};

    const ScoreImportSummary summary =
        summariseScoreImport(resultOf(std::move(score), MusicXmlImportStatus::imported));

    CHECK_FALSE(hasField(summary, "Work"));
    CHECK_FALSE(hasField(summary, "Movement"));
    CHECK_FALSE(hasField(summary, "Composer"));
    CHECK_FALSE(hasField(summary, "Lyricist"));
    CHECK_FALSE(hasField(summary, "Written by"));

    // No field is present with an empty value either -- the absent ones are
    // left out of the vector, not blanked.
    for (const ScoreImportField& field : summary.fields)
    {
        CHECK_FALSE(field.value.empty());
    }

    CHECK(summary.measureCount == 2);
    CHECK(valueOf(summary, "Measures") == "2");

    // An unnamed part still has to be identifiable, so its id stands in.
    REQUIRE(summary.parts.size() == 1);
    CHECK(summary.parts.front().name == "P1");
}

TEST_CASE("total length is reported in bars and in seconds", "[score][import][summary]")
{
    // Four bars of 4/4 at the default 120 BPM: a quarter note is half a second,
    // so a bar is two seconds.
    const ScoreImportSummary summary =
        summariseScoreImport(resultOf(makeVocalScore(), MusicXmlImportStatus::imported));

    CHECK(summary.length.bars == 4);
    CHECK(summary.length.seconds == Approx(8.0));
    CHECK(summary.startingBeatsPerMinute == Approx(score::defaultBeatsPerMinute));
    CHECK(valueOf(summary, "Length") == "4 bars, 0:08");
    CHECK(valueOf(summary, "Tempo") == "120 BPM");
}

TEST_CASE("seconds follow the file's tempo rather than the default", "[score][import][summary]")
{
    Score score = makeVocalScore();
    score.tempoMap = TempoMap::build({{0, 60.0}});

    const ScoreImportSummary summary =
        summariseScoreImport(resultOf(std::move(score), MusicXmlImportStatus::imported));

    // At 60 BPM a bar of 4/4 is four seconds. This is the assertion that proves
    // the tempo map was read rather than defaulted.
    CHECK(summary.length.seconds == Approx(16.0));
    CHECK(summary.startingBeatsPerMinute == Approx(60.0));
    CHECK(valueOf(summary, "Length") == "4 bars, 0:16");
    CHECK(valueOf(summary, "Tempo") == "60 BPM");
}

TEST_CASE("seconds accumulate across a mid-score tempo change", "[score][import][summary]")
{
    Score score = makeVocalScore();
    score.tempoMap = TempoMap::build({{0, 60.0}, {barTicks * 2, 120.0}});

    const ScoreImportSummary summary =
        summariseScoreImport(resultOf(std::move(score), MusicXmlImportStatus::imported));

    // Two bars at 60 BPM (four seconds each) then two at 120 (two each).
    CHECK(summary.length.seconds == Approx(12.0));

    // The tempo shown is the one in force at the start, not the last one read.
    CHECK(summary.startingBeatsPerMinute == Approx(60.0));
    CHECK(valueOf(summary, "Length") == "4 bars, 0:12");
}

TEST_CASE("a tempo that is not a whole number keeps its decimal", "[score][import][summary]")
{
    Score score = makeVocalScore();
    score.tempoMap = TempoMap::build({{0, 132.5}});

    const ScoreImportSummary summary =
        summariseScoreImport(resultOf(std::move(score), MusicXmlImportStatus::imported));

    CHECK(valueOf(summary, "Tempo") == "132.5 BPM");
}

TEST_CASE("a long score's length reads as minutes and seconds", "[score][import][summary]")
{
    Score score;
    score.parts = {partOf("P1", "Organ", 2, 100)};

    const ScoreImportSummary summary =
        summariseScoreImport(resultOf(std::move(score), MusicXmlImportStatus::imported));

    // 100 bars of 4/4 at 120 BPM is 200 seconds.
    CHECK(summary.length.seconds == Approx(200.0));
    CHECK(valueOf(summary, "Length") == "100 bars, 3:20");
}

TEST_CASE(
    "diagnostics are grouped by severity, in the importer's order within a group",
    "[score][import][summary]")
{
    std::vector<Diagnostic> diagnostics{
        diagnosticOf(DiagnosticSeverity::info, "Ignored <print>", "print"),
        diagnosticOf(DiagnosticSeverity::repaired, "Over-full measure truncated"),
        diagnosticOf(DiagnosticSeverity::unsupported, "Dropped <harmony>", "harmony"),
        diagnosticOf(DiagnosticSeverity::repaired, "Dangling tie dropped"),
    };

    const ScoreImportSummary summary = summariseScoreImport(
        resultOf(makeVocalScore(), MusicXmlImportStatus::importedWithDiagnostics, diagnostics));

    REQUIRE(summary.diagnostics.size() == 3);

    CHECK(summary.diagnostics[0].severity == DiagnosticSeverity::unsupported);
    REQUIRE(summary.diagnostics[0].lines.size() == 1);
    CHECK(summary.diagnostics[0].lines.front().message == "Dropped <harmony>");
    CHECK(summary.diagnostics[0].lines.front().elementName == "harmony");

    CHECK(summary.diagnostics[1].severity == DiagnosticSeverity::repaired);
    REQUIRE(summary.diagnostics[1].lines.size() == 2);

    // Source order within the group, so a user reading the list reads the file
    // in the order the importer read it.
    CHECK(summary.diagnostics[1].lines[0].message == "Over-full measure truncated");
    CHECK(summary.diagnostics[1].lines[1].message == "Dangling tie dropped");

    CHECK(summary.diagnostics[2].severity == DiagnosticSeverity::info);
    REQUIRE(summary.diagnostics[2].lines.size() == 1);

    CHECK_FALSE(summary.cleanImport);
    CHECK(summary.diagnosticsMessage.empty());
}

TEST_CASE("a severity with no diagnostics produces no group", "[score][import][summary]")
{
    const ScoreImportSummary summary = summariseScoreImport(resultOf(
        makeVocalScore(), MusicXmlImportStatus::importedWithDiagnostics,
        {diagnosticOf(DiagnosticSeverity::info, "Ignored <print>", "print")}));

    REQUIRE(summary.diagnostics.size() == 1);
    CHECK(summary.diagnostics.front().severity == DiagnosticSeverity::info);
}

TEST_CASE(
    "a diagnostic's location names the part the user sees, with the printed bar",
    "[score][import][summary]")
{
    Diagnostic located =
        diagnosticOf(DiagnosticSeverity::unsupported, "Dropped <transpose>", "transpose");
    located.location.partId = "P3";
    located.location.measureNumber = "12a";
    located.location.voice = 2;

    Diagnostic documentLevel =
        diagnosticOf(DiagnosticSeverity::info, "Unrecognised <credit>", "credit");

    const ScoreImportSummary summary = summariseScoreImport(resultOf(
        makeVocalScore(), MusicXmlImportStatus::importedWithDiagnostics, {located, documentLevel}));

    REQUIRE(summary.diagnostics.size() == 2);

    // "P3" resolves to the part's display name; the bar is quoted exactly as
    // the file printed it, split bar suffix and all.
    CHECK(summary.diagnostics[0].lines.front().location == "Piano, bar 12a, voice 2");

    // A document-level diagnostic has no location to show.
    CHECK(summary.diagnostics[1].lines.front().location.empty());
}

TEST_CASE(
    "a diagnostic seen many times reports its count rather than repeating",
    "[score][import][summary]")
{
    Diagnostic repeated = diagnosticOf(DiagnosticSeverity::info, "Unrecognised <print>", "print");
    repeated.occurrences = 2114;

    const ScoreImportSummary summary = summariseScoreImport(
        resultOf(makeVocalScore(), MusicXmlImportStatus::importedWithDiagnostics, {repeated}));

    REQUIRE(summary.diagnostics.size() == 1);
    REQUIRE(summary.diagnostics.front().lines.size() == 1);
    CHECK(summary.diagnostics.front().lines.front().occurrences == 2114);
}

TEST_CASE("a clean import says so explicitly", "[score][import][summary]")
{
    const ScoreImportSummary summary =
        summariseScoreImport(resultOf(makeVocalScore(), MusicXmlImportStatus::imported));

    CHECK(summary.cleanImport);
    CHECK(summary.diagnostics.empty());

    // An empty list and a failure to report are indistinguishable on screen, so
    // the clean case gets a sentence of its own.
    CHECK(summary.diagnosticsMessage == "Nothing was dropped or repaired.");
}

TEST_CASE(
    "every failure status reports its own reason and the importer's message",
    "[score][import][summary]")
{
    std::vector<std::string> headlines;

    for (const MusicXmlImportStatus status : everyStatus)
    {
        MusicXmlImportResult result;
        result.status = status;

        if (score::musicxml::succeeded(status))
        {
            result.score = std::make_shared<const Score>(makeVocalScore());
        }
        else
        {
            result.error =
                "the importer's own words about " + std::to_string(static_cast<int>(status));
        }

        const ScoreImportSummary summary = summariseScoreImport(result);

        CHECK(summary.status == status);
        CHECK(summary.succeeded == score::musicxml::succeeded(status));
        CHECK_FALSE(summary.headline.empty());

        // Verbatim, not paraphrased: the importer names the container entry
        // that was missing or the document type that was not read, and nothing
        // re-worded here could add that.
        CHECK(summary.detail == result.error);

        headlines.push_back(summary.headline);
    }

    // Distinct per status. A generic "could not open file" for several statuses
    // would discard the work the importer did to tell them apart.
    std::sort(headlines.begin(), headlines.end());
    CHECK(std::adjacent_find(headlines.begin(), headlines.end()) == headlines.end());
}

TEST_CASE("a failed import describes no score", "[score][import][summary]")
{
    MusicXmlImportResult result;
    result.status = MusicXmlImportStatus::malformedXml;
    result.error = "Unexpected end of document at <note>";

    const ScoreImportSummary summary = summariseScoreImport(result);

    CHECK_FALSE(summary.succeeded);
    CHECK(summary.fields.empty());
    CHECK(summary.parts.empty());
    CHECK(summary.measureCount == 0);
    CHECK(summary.length.bars == 0);
    CHECK(summary.length.seconds == Approx(0.0));
    CHECK(summary.detail == "Unexpected end of document at <note>");

    // The "nothing was dropped or repaired" reassurance belongs to a success;
    // on a failure the headline has already said what happened.
    CHECK(summary.diagnosticsMessage.empty());
}

TEST_CASE(
    "a failed import still shows what the importer managed to say",
    "[score][import][summary]")
{
    Diagnostic located =
        diagnosticOf(DiagnosticSeverity::repaired, "Generated an id for an unnamed part");
    located.location.partId = "P7";
    located.location.measureNumber = "1";

    MusicXmlImportResult result;
    result.status = MusicXmlImportStatus::structurallyInvalid;
    result.error = "No <part> matched <score-part> P7";
    result.diagnostics = {located};

    const ScoreImportSummary summary = summariseScoreImport(result);

    REQUIRE(summary.diagnostics.size() == 1);

    // With no score to resolve against, the part id stands in -- still the id
    // the file used, so still findable.
    CHECK(summary.diagnostics.front().lines.front().location == "P7, bar 1");
}

TEST_CASE("the report lists the fields and the parts", "[score][import][summary]")
{
    const std::string report = scoreImportReport(
        summariseScoreImport(resultOf(makeVocalScore(), MusicXmlImportStatus::imported)));

    CHECK(report.find("Work: Requiem") != std::string::npos);
    CHECK(report.find("Written by: MuseScore 4.4.2") != std::string::npos);
    CHECK(report.find("Measures: 4") != std::string::npos);
    CHECK(report.find("Soprano") != std::string::npos);

    // A multi-staff part says so; a single-staff part does not say "1 staff",
    // which would be noise on every vocal line.
    CHECK(report.find("Piano (2 staves)") != std::string::npos);
    CHECK(report.find("Soprano (1") == std::string::npos);
}

TEST_CASE(
    "the report states a clean import rather than showing nothing",
    "[score][import][summary]")
{
    const std::string report = scoreImportReport(
        summariseScoreImport(resultOf(makeVocalScore(), MusicXmlImportStatus::imported)));

    CHECK(report.find("Nothing was dropped or repaired.") != std::string::npos);
}

TEST_CASE(
    "the report groups diagnostics under headings that say what they mean",
    "[score][import][summary]")
{
    Diagnostic dropped =
        diagnosticOf(DiagnosticSeverity::unsupported, "Dropped <harmony>", "harmony");
    dropped.location.partId = "P3";
    dropped.location.measureNumber = "12a";

    Diagnostic repeated = diagnosticOf(DiagnosticSeverity::info, "Unrecognised <print>", "print");
    repeated.occurrences = 2114;

    const std::string report = scoreImportReport(summariseScoreImport(resultOf(
        makeVocalScore(), MusicXmlImportStatus::importedWithDiagnostics, {dropped, repeated})));

    CHECK(report.find("does not support") != std::string::npos);
    CHECK(report.find("Notes about the file:") != std::string::npos);
    CHECK(report.find("Dropped <harmony> [Piano, bar 12a]") != std::string::npos);
    CHECK(report.find("(seen 2114 times)") != std::string::npos);

    // The clean-import sentence must not appear when there are diagnostics.
    CHECK(report.find("Nothing was dropped") == std::string::npos);
}

TEST_CASE("the report leads with the importer's message on a failure", "[score][import][summary]")
{
    MusicXmlImportResult result;
    result.status = MusicXmlImportStatus::invalidContainer;
    result.error = "META-INF/container.xml names score.xml, which the container does not hold";

    const std::string report = scoreImportReport(summariseScoreImport(result));

    CHECK(report.find("META-INF/container.xml names score.xml") == 0);

    // Nothing structural to report, so nothing is invented.
    CHECK(report.find("Measures:") == std::string::npos);
}
