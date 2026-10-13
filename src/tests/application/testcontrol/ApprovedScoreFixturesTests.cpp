#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>

#include "application/shell/ui/score/ScoreImportSummary.h"
#include "application/testcontrol/ApprovedWindowStates.h"
#include "platform/score/musicxml/MusicXmlImporter.h"

// The scores two approved states name, checked on disk and through the
// importer.
//
// Separate from ApprovedWindowStatesTests because this one touches the
// filesystem: that file asserts on the vocabulary alone and has no JUCE
// dependency, and this one is the reason a drifted fixture fails a build
// instead of producing a capture of an empty window that a reviewer might
// approve.
using namespace score;
using namespace score::musicxml;
using namespace testcontrol;

namespace
{
juce::File fixtureDirectory()
{
    return juce::File(PRACTICE_TAKES_SOURCE_DIR)
        .getChildFile(juce::String(approvedScoreFixtureDirectory()));
}

[[nodiscard]] MusicXmlImportResult importFixture(const std::string& name)
{
    const juce::File file = fixtureDirectory().getChildFile(juce::String(name));
    REQUIRE(file.existsAsFile());

    return importMusicXmlFile(file);
}

[[nodiscard]] std::map<DiagnosticSeverity, int> countBySeverity(const MusicXmlImportResult& result)
{
    std::map<DiagnosticSeverity, int> counts;

    for (const Diagnostic& diagnostic : result.diagnostics)
    {
        ++counts[diagnostic.severity];
    }

    return counts;
}
} // namespace

TEST_CASE("every state's score fixture is on disk", "[testcontrol][approved][score]")
{
    // The failure mode this exists for: a state pointing at a file that is not
    // there. It names both the state and the path it looked at, because
    // "missing fixture" without either is the least useful way to learn this.
    for (const ApprovedWindowState& state : approvedWindowStates())
    {
        if (state.scoreFixture.empty())
        {
            continue;
        }

        const juce::File file = fixtureDirectory().getChildFile(juce::String(state.scoreFixture));

        INFO(
            "state \"" << state.id << "\" names the score fixture \"" << state.scoreFixture
                       << "\", which should be at " << file.getFullPathName());
        CHECK(file.existsAsFile());
    }
}

TEST_CASE(
    "the two score import states are the ones naming a fixture",
    "[testcontrol][approved][score]")
{
    std::set<std::string> naming;

    for (const ApprovedWindowState& state : approvedWindowStates())
    {
        if (!state.scoreFixture.empty())
        {
            naming.insert(state.id);
        }
    }

    // Both halves matter. The first is that the two surfaces exist; the second
    // is that the field stayed empty everywhere else, which is what makes
    // "every other state behaves exactly as before" a checked claim rather
    // than an assumption.
    CHECK(naming == std::set<std::string>{"score-import-clean", "score-import-diagnostics"});
}

TEST_CASE("a state naming a score opens nothing else", "[testcontrol][approved][score]")
{
    // The summary is its own window. A tool, the settings window, or the
    // feedback form open behind it would put a second subject in the picture
    // and make the prompt ambiguous.
    for (const ApprovedWindowState& state : approvedWindowStates())
    {
        if (state.scoreFixture.empty())
        {
            continue;
        }

        INFO("state " << state.id);
        CHECK(state.tools.empty());
        CHECK_FALSE(state.settingsOpen);
        CHECK_FALSE(state.feedbackOpen);
    }
}

TEST_CASE("the clean fixture imports with nothing to report", "[testcontrol][approved][score]")
{
    const MusicXmlImportResult result = importFixture("clean-import.musicxml");

    // Not "it succeeded": the surface is of the "Nothing was dropped or
    // repaired." branch, which only exists when the diagnostics list is empty.
    CHECK(result.status == MusicXmlImportStatus::imported);
    CHECK(result.diagnostics.empty());

    REQUIRE(result.score != nullptr);

    // The metadata the summary shows, field by field, because a fixture that
    // quietly stops carrying a composer makes the picture stop covering the
    // line that prints one.
    CHECK(result.score->metadata.workTitle == "Duet for the Capture Harness");
    CHECK_FALSE(result.score->metadata.composer.empty());
    CHECK_FALSE(result.score->metadata.lyricist.empty());
    CHECK_FALSE(result.score->metadata.encodingSoftware.empty());

    REQUIRE(result.score->parts.size() == 2);
    CHECK_FALSE(result.score->parts[0].name.empty());

    // One part on two staves, so the summary's "(2 staves)" suffix is in the
    // capture rather than only in a unit test.
    const auto& parts = result.score->parts;
    CHECK(std::any_of(
        parts.begin(), parts.end(), [](const Part& part) { return part.staffCount == 2; }));
}

TEST_CASE(
    "the diagnostic fixture reports in more than one severity",
    "[testcontrol][approved][score]")
{
    const MusicXmlImportResult result = importFixture("diagnostic-import.musicxml");

    CHECK(result.status == MusicXmlImportStatus::importedWithDiagnostics);
    REQUIRE(result.score != nullptr);

    const std::map<DiagnosticSeverity, int> counts = countBySeverity(result);

    // One group per severity in the window, so a picture of this score is a
    // picture of all three headings.
    CHECK(counts.count(DiagnosticSeverity::unsupported) == 1);
    CHECK(counts.count(DiagnosticSeverity::repaired) == 1);
    CHECK(counts.count(DiagnosticSeverity::info) == 1);
}

TEST_CASE("the diagnostic fixture carries the regression picture", "[testcontrol][approved][score]")
{
    // The defect this surface exists for: several unrecognised elements share
    // one generic message, and the element name is the only thing telling the
    // lines apart. Before the name was printed, a real MuseScore export
    // rendered seven identical rows naming nothing a user could act on.
    const MusicXmlImportResult result = importFixture("diagnostic-import.musicxml");

    std::set<std::string> elements;
    std::set<std::string> messages;

    for (const Diagnostic& diagnostic : result.diagnostics)
    {
        if (diagnostic.severity != DiagnosticSeverity::info)
        {
            continue;
        }

        elements.insert(diagnostic.elementName);
        messages.insert(diagnostic.message);
    }

    // Several lines, one message between them, a distinct element name each.
    CHECK(elements.size() >= 4);
    CHECK(messages.size() == 1);
    CHECK(elements.count("") == 0);
}

TEST_CASE("report what the summary window will show for each fixture", "[.harness-score-report]")
{
    // Hidden and opt-in, following [.corpus-report]:
    //
    //     PracticeTakesTests "[.harness-score-report]" --success
    //
    // The capture of these two surfaces is a picture of exactly this text. Any
    // question about what the reference image should contain is answerable here
    // without building the application or taking a screenshot.
    for (const ApprovedWindowState& state : approvedWindowStates())
    {
        if (state.scoreFixture.empty())
        {
            continue;
        }

        const MusicXmlImportResult result = importFixture(state.scoreFixture);
        const ScoreImportSummary summary = summariseScoreImport(result);

        WARN(
            state.id << " -- " << state.scoreFixture << "\n  " << summary.headline << "\n"
                     << scoreImportReport(summary));
    }
}
