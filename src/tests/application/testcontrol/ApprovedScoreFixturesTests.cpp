#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>

#include "application/shell/ui/score/ScoreImportSummary.h"
#include "application/testcontrol/ApprovedWindowStates.h"
#include "features/score/engraving/ScoreLayoutEngine.h"
#include "platform/score/musicxml/MusicXmlImporter.h"

// The scores the approved states name, checked on disk, through the importer,
// and -- for the one a renderer draws -- through the layout.
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

TEST_CASE("the score states are the ones naming a fixture", "[testcontrol][approved][score]")
{
    std::set<std::string> naming;

    for (const ApprovedWindowState& state : approvedWindowStates())
    {
        if (!state.scoreFixture.empty())
        {
            naming.insert(state.id);
        }
    }

    // Both halves matter. The first is that the surfaces exist; the second is
    // that the field stayed empty everywhere else, which is what makes "every
    // other state behaves exactly as before" a checked claim rather than an
    // assumption.
    CHECK(
        naming == std::set<std::string>{
                      "score-import-clean", "score-import-diagnostics", "score-view-notation",
                      "score-view-notation-large"});
}

// The invariant this started as was "a state naming a score opens nothing
// else", which protected one subject per picture. It is kept and restated
// rather than dropped, because the thing it was protecting against -- a second
// window in the frame -- is still true of the summary.
TEST_CASE("one subject per score picture", "[testcontrol][approved][score]")
{
    for (const ApprovedWindowState& state : approvedWindowStates())
    {
        if (state.scoreFixture.empty())
        {
            continue;
        }

        INFO("state " << state.id);

        // Neither kind of score surface wants the settings window or the
        // feedback form behind it.
        CHECK_FALSE(state.settingsOpen);
        CHECK_FALSE(state.feedbackOpen);

        if (state.showScoreImportSummary)
        {
            // The summary is its own window, so a tool open behind it would
            // put a second subject in the picture.
            CHECK(state.tools.empty());

            continue;
        }

        // A state that suppresses the summary is about what a tool draws, so
        // it needs exactly one -- and suppressing the summary while opening no
        // tool would import a score for nobody to look at.
        CHECK(state.tools.size() == 1);
    }
}

// The field is not a general switch: only a state that names a fixture has a
// summary to suppress, so anything else setting it would be a mistake that
// reads as deliberate.
TEST_CASE("only a score state suppresses the summary", "[testcontrol][approved][score]")
{
    for (const ApprovedWindowState& state : approvedWindowStates())
    {
        if (!state.scoreFixture.empty())
        {
            continue;
        }

        INFO("state " << state.id);
        CHECK(state.showScoreImportSummary);
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

// The fixture that exists to be a picture of notation. Pinned the same way the
// other two are pinned against the summary: through the thing that will draw
// it, so that a fixture quietly losing a construct fails a build rather than
// changing a reference image into one of less.
TEST_CASE("the notation fixture holds what its picture is about", "[testcontrol][approved][score]")
{
    const MusicXmlImportResult result = importFixture("notation-sample.musicxml");

    // Clean, like clean-import.musicxml: the surface is of the renderer, not
    // of a report, so a diagnostic here means the fixture drifted outside the
    // supported subset.
    CHECK(result.status == MusicXmlImportStatus::imported);

    for (const Diagnostic& diagnostic : result.diagnostics)
    {
        INFO(
            "unexpected diagnostic on <"
            << diagnostic.elementName << "> in bar "
            << diagnostic.location.measureNumber.value_or("-") << ": " << diagnostic.message);
        CHECK(false);
    }

    REQUIRE(result.score != nullptr);
    CHECK(result.score->metadata.workTitle == "Notation Sample");
    REQUIRE(result.score->parts.size() == 1);
    CHECK(result.score->parts.front().name == "Melody");
    CHECK(measureCount(*result.score) == 8);

    // Pinned on the model before the layout, so a tie that never survived
    // import is told apart from one the layout failed to draw.
    std::size_t modelTies = 0;

    for (const Measure& measure : result.score->parts.front().measures)
    {
        for (const Voice& voice : measure.voices)
        {
            for (const ScoreEvent& event : voice.events)
            {
                for (const Note& note : event.notes)
                {
                    modelTies += note.tiedTo.has_value() ? 1 : 0;
                }
            }
        }
    }

    CHECK(modelTies == 1);

    // Wide enough for the whole fixture on one system, so that this case is
    // about what the fixture holds rather than about where the systems break.
    // A tie whose two notes land on different systems is not drawn, which is
    // the layout's own documented behaviour and has its own test.
    engraving::LayoutOptions options;
    options.availableWidth = 400.0;

    const engraving::ScoreLayout layout = engraving::layOutScore(result.score, options);

    REQUIRE(layout.outcome == engraving::LayoutOutcome::laidOut);
    REQUIRE_FALSE(layout.systems.empty());

    std::size_t noteheads = 0;
    std::size_t rests = 0;
    std::size_t ties = 0;
    std::size_t clefs = 0;
    std::size_t keyAccidentals = 0;
    std::size_t lyrics = 0;
    std::size_t graceNotes = 0;
    std::size_t dottedNotes = 0;
    std::size_t flaggedStems = 0;
    std::size_t accidentals = 0;
    std::size_t naturals = 0;
    std::size_t ledgered = 0;
    std::size_t repeatBarlines = 0;
    std::size_t dynamics = 0;
    std::size_t tempoMarks = 0;
    std::size_t beams = 0;
    std::size_t secondBeams = 0;
    std::size_t partialBeams = 0;
    std::size_t beamedStems = 0;

    for (const engraving::System& system : layout.systems)
    {
        noteheads += system.noteheads.size();
        rests += system.rests.size();
        ties += system.ties.size();
        clefs += system.clefs.size();
        keyAccidentals += system.keyAccidentals.size();
        lyrics += system.lyrics.size();

        for (const engraving::NoteheadGlyph& glyph : system.noteheads)
        {
            graceNotes += glyph.isGrace ? 1 : 0;
            dottedNotes += glyph.dots > 0 ? 1 : 0;
            ledgered += (glyph.ledger.above + glyph.ledger.below) > 0 ? 1 : 0;

            if (glyph.accidental != engraving::AccidentalKind::none)
            {
                ++accidentals;
            }

            naturals += glyph.accidental == engraving::AccidentalKind::natural ? 1 : 0;
        }

        for (const engraving::StemGlyph& glyph : system.stems)
        {
            flaggedStems += glyph.flags > 0 ? 1 : 0;
            beamedStems += glyph.beamLevels > 0 ? 1 : 0;
        }

        beams += system.beams.size();

        for (const engraving::BeamGlyph& glyph : system.beams)
        {
            secondBeams += glyph.level == 2 ? 1 : 0;
            partialBeams += glyph.partial ? 1 : 0;
        }

        for (const engraving::BarlineGlyph& glyph : system.barlines)
        {
            if (glyph.kind == engraving::BarlineKind::repeatStart ||
                glyph.kind == engraving::BarlineKind::repeatEnd)
            {
                ++repeatBarlines;
            }
        }

        for (const engraving::TextGlyph& glyph : system.texts)
        {
            dynamics += glyph.role == engraving::TextRole::dynamic ? 1 : 0;
            tempoMarks += glyph.role == engraving::TextRole::tempo ? 1 : 0;
        }
    }

    // One assertion per construct the fixture's comment promises, so a failure
    // names the thing that went missing.
    INFO("noteheads " << noteheads << ", rests " << rests << ", ties " << ties);

    CHECK(noteheads == 25);
    CHECK(rests == 1);
    CHECK(ties == 1);

    // The opening treble clef and the change to bass. More than two means the
    // layout broke into systems, which repeats the clef at each start.
    CHECK(clefs >= 2);

    // Two sharps, once per system start.
    CHECK(keyAccidentals >= 2);
    CHECK(keyAccidentals % 2 == 0);

    CHECK(lyrics == 13);
    CHECK(layout.lyricVerseCount == 1);

    CHECK(graceNotes == 1);
    CHECK(dottedNotes == 1);

    // Bar 1's pair of eighths share one beat and so beam; bar 3's pair of
    // sixteenths do the same and carry a second beam. Everything else short
    // enough to take a tail is alone in its beat and keeps its flags: the
    // grace note, and bar 3's two eighths either side of the sixteenths.
    CHECK(flaggedStems == 3);
    CHECK(beamedStems == 4);
    CHECK(beams == 3);
    CHECK(secondBeams == 1);

    // No partial beam: nothing in the fixture is a lone sixteenth among
    // eighths. `drawBeam` branches on nothing a stub changes -- a partial beam
    // is the same filled quad between two x values -- so the picture of the
    // full beams is a picture of that code path too, and where a stub is
    // placed is pinned in BeamGeometryTests.
    CHECK(partialBeams == 0);

    // The B-flat is signed, the B-flat after it is not, and the B after that
    // is cancelled -- the whole in-bar accidental rule, in one picture.
    CHECK(accidentals == 2);
    CHECK(naturals == 1);

    CHECK(ledgered >= 1);
    CHECK(repeatBarlines == 2);
    // No dynamic: a <dynamics> element would make this fixture import with a
    // diagnostic, which the file's own comment explains. The renderer's
    // handling of one is covered in ScoreLayoutEngineTests.
    CHECK(dynamics == 0);
    CHECK(tempoMarks == 1);
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
