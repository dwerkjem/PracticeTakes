#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>

#include "application/shell/ui/score/ScoreImportSummary.h"
#include "application/testcontrol/ApprovedWindowStates.h"
#include "platform/score/engraving/ScoreLayout.h"
#include "platform/score/musicxml/MusicXmlImporter.h"

// The scores the approved states name, checked on disk and through the
// importer -- and, for the one that is about a page rather than a report,
// through the engraving layer as well.
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
    "the states naming a fixture are the three that want one",
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

    // Both halves matter. The first is that the three surfaces exist; the
    // second is that the field stayed empty everywhere else, which is what
    // makes "every other state behaves exactly as before" a checked claim
    // rather than an assumption.
    CHECK(
        naming ==
        std::set<std::string>{"score-import-clean", "score-import-diagnostics", "score-view-page"});
}

TEST_CASE("only a state naming a fixture hides the summary", "[testcontrol][approved][score]")
{
    // `scoreSummaryHidden` on a state that imports nothing would be a field
    // with no effect, which is the shape a positional-initialiser mistake
    // takes: a value landing one slot off reads as a plausible flag on the
    // wrong state.
    for (const ApprovedWindowState& state : approvedWindowStates())
    {
        if (state.scoreSummaryHidden)
        {
            INFO("state " << state.id << " hides the summary but names no fixture");
            CHECK_FALSE(state.scoreFixture.empty());
        }
    }

    const ApprovedWindowState* page = findApprovedWindowState("score-view-page");

    REQUIRE(page != nullptr);

    // The one state whose subject is the page rather than the report, stated
    // positively so that losing the flag fails here rather than in a capture
    // nobody can explain.
    CHECK(page->scoreSummaryHidden);
    CHECK(page->scoreFixture == "engraved-page.musicxml");

    for (const std::string& id : {"score-import-clean", "score-import-diagnostics"})
    {
        const ApprovedWindowState* summary = findApprovedWindowState(id);

        REQUIRE(summary != nullptr);
        INFO("state " << id);
        CHECK_FALSE(summary->scoreSummaryHidden);
    }
}

TEST_CASE("a state about the summary window opens nothing else", "[testcontrol][approved][score]")
{
    // The summary is its own window. A tool, the settings window, or the
    // feedback form open behind it would put a second subject in the picture
    // and make the prompt ambiguous.
    //
    // A state that hides the summary is exempt by construction: its whole
    // point is a tool with the score in it, so it must open one.
    for (const ApprovedWindowState& state : approvedWindowStates())
    {
        if (state.scoreFixture.empty() || state.scoreSummaryHidden)
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

TEST_CASE(
    "the engraved page fixture imports with nothing to report",
    "[testcontrol][approved][score]")
{
    const MusicXmlImportResult result = importFixture("engraved-page.musicxml");

    // A diagnostic would change the picture: the score view shows a count of
    // them, so a fixture that starts importing with something to say turns the
    // reference image into one with a notice on it.
    CHECK(result.status == MusicXmlImportStatus::imported);
    CHECK(result.diagnostics.empty());

    REQUIRE(result.score != nullptr);

    const Score& score = *result.score;

    REQUIRE(score.parts.size() == 2);

    // A single-staff part above a braced two-staff one, which is the shape the
    // surface is about.
    CHECK(score.parts[0].staffCount == 1);
    CHECK(score.parts[1].staffCount == 2);

    REQUIRE(score.parts[0].measures.size() == 12);
    REQUIRE(score.parts[1].measures.size() == 12);

    // The two key signatures and the mid-score clef change, pinned at the bars
    // the fixture's comment names. Each is a thing a reviewer is asked to find
    // in the picture, so each has to survive a change to the importer.
    REQUIRE(score.parts[0].measures[0].attributes.key.has_value());
    CHECK(score.parts[0].measures[0].attributes.key->fifths == 2);

    REQUIRE(score.parts[0].measures[4].attributes.key.has_value());
    CHECK(score.parts[0].measures[4].attributes.key->fifths == -3);

    REQUIRE(score.parts[0].measures[0].attributes.time.has_value());
    CHECK(score.parts[0].measures[0].attributes.time->beats == 3);
    CHECK(score.parts[0].measures[0].attributes.time->beatType == 4);

    const AttributeChanges& barSeven = score.parts[1].measures[6].attributes;

    REQUIRE(barSeven.clefs.size() == 1);
    CHECK(barSeven.clefs.front().staff == 2);
    CHECK(barSeven.clefs.front().sign == "G");
}

TEST_CASE(
    "the engraved page fixture lays out to a page worth photographing",
    "[testcontrol][approved][score]")
{
    // The other half of the pin. The test above says the *model* still carries
    // what the surface claims; this one says the renderer still turns it into
    // the page a reviewer is asked about. Between them, a change that would
    // have silently emptied the reference image fails a build instead.
    const MusicXmlImportResult result = importFixture("engraved-page.musicxml");

    REQUIRE(result.score != nullptr);

    using namespace score::engraving;

    // The width the score view hands the layout for a docked panel in a
    // 1280x800 window, near enough: what matters is that it is narrow enough
    // to break.
    LayoutOptions options;
    options.availableWidth = 110.0;

    const ScoreLayout layout = layOutScore(*result.score, options);

    // Nothing the renderer had to refuse to draw. Same reasoning as the
    // import: the tool shows a count.
    CHECK(layout.diagnostics.empty());

    // More than one system, so a later system's head is in the picture.
    REQUIRE(layout.systems.size() > 1);

    bool sawBrace = false;
    bool sawChordOfThree = false;
    bool sawLedgerLine = false;
    bool sawSharpKey = false;
    bool sawFlatKey = false;

    for (const SystemLayout& system : layout.systems)
    {
        REQUIRE(system.parts.size() == 2);

        // One staff above two, every system.
        CHECK(system.parts[0].staves.size() == 1);
        CHECK(system.parts[1].staves.size() == 2);

        sawBrace = sawBrace || system.parts[1].braced;

        for (const PartLayout& part : system.parts)
        {
            for (const StaffLayout& staff : part.staves)
            {
                // A head on every staff of every system: the clef, and a key
                // signature in whichever direction is in force there.
                std::size_t sharps = 0;
                std::size_t flats = 0;
                std::size_t noteheadsAtFirstX = 0;
                double firstNoteheadX = 0.0;
                bool haveFirst = false;

                for (const PlacedGlyph& glyph : staff.glyphs)
                {
                    if (glyph.glyph == Glyph::accidentalSharp)
                    {
                        ++sharps;
                    }
                    else if (glyph.glyph == Glyph::accidentalFlat)
                    {
                        ++flats;
                    }
                    else if (glyph.glyph == Glyph::noteheadHalf)
                    {
                        if (!haveFirst)
                        {
                            firstNoteheadX = glyph.x;
                            haveFirst = true;
                        }

                        if (glyph.x == firstNoteheadX)
                        {
                            ++noteheadsAtFirstX;
                        }
                    }
                }

                sawSharpKey = sawSharpKey || sharps >= 2;
                sawFlatKey = sawFlatKey || flats >= 3;
                sawChordOfThree = sawChordOfThree || noteheadsAtFirstX >= 3;
                sawLedgerLine = sawLedgerLine || !staff.ledgerLines.empty();
            }
        }
    }

    CHECK(sawBrace);
    CHECK(sawSharpKey);
    CHECK(sawFlatKey);
    CHECK(sawChordOfThree);
    CHECK(sawLedgerLine);
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
        // Skipping the state that hides the summary: there is no summary
        // window in that capture, so a report of its text would describe
        // nothing in the picture.
        if (state.scoreFixture.empty() || state.scoreSummaryHidden)
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
