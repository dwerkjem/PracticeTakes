#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "application/shell/ui/score/ScoreImportSummary.h"
#include "application/testcontrol/ApprovedWindowStates.h"
#include "features/score/ScoreViewSettings.h"
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
        // Not a file in the directory: the shipped sample, whose own tests
        // cover that it is there and imports.
        if (state.scoreFixture.empty() || state.scoreFixture == bundledSampleFixtureName())
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
                      "score-import-clean", "score-import-diagnostics", "score-import-in-progress",
                      "score-view-engraving", "score-view-grand-staff", "score-view-horizontal",
                      "score-view-notation", "score-view-part-hidden", "score-view-notation-large",
                      "score-view-page", "score-view-sample", "score-view-vertical"});
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

// Holding an import is only meaningful for a state that names something to
// import and shows the window it would be read in -- and one such picture is
// enough. A held import with the window suppressed would be a capture of an
// empty desk.
TEST_CASE("one state holds an import in progress, and shows it", "[testcontrol][approved][score]")
{
    std::vector<std::string> holding;

    for (const ApprovedWindowState& state : approvedWindowStates())
    {
        if (!state.holdScoreImportInProgress)
        {
            continue;
        }

        INFO("state " << state.id);
        holding.push_back(state.id);
        CHECK_FALSE(state.scoreFixture.empty());
        CHECK(state.showScoreImportSummary);
        CHECK(state.tools.empty());
    }

    CHECK(holding == std::vector<std::string>{"score-import-in-progress"});
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
    // A tie whose two notes land on different systems is drawn as two halves,
    // which would count twice here; that has its own tests.
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
        ledgered += system.ledgerLines.size();
        rests += system.rests.size();
        ties += system.ties.size();
        clefs += system.clefs.size();
        keyAccidentals += system.keyAccidentals.size();
        lyrics += system.lyrics.size();

        for (const engraving::NoteheadGlyph& glyph : system.noteheads)
        {
            graceNotes += glyph.isGrace ? 1 : 0;
            dottedNotes += glyph.dots > 0 ? 1 : 0;

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
        if (state.scoreFixture.empty() || state.scoreFixture == bundledSampleFixtureName())
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

// The piano fixture for the grand-staff surface. Pinned through the importer and
// the layout, like the notation fixture, so that the file quietly losing a
// staff, a clef change or its alignment case fails a build rather than turning
// its reference image into a picture of less.
TEST_CASE(
    "the grand-staff fixture holds what its picture is about",
    "[testcontrol][approved][score]")
{
    const MusicXmlImportResult result = importFixture("grand-staff-sample.musicxml");

    // Clean: the surface is of the renderer, so a diagnostic means the fixture
    // drifted outside the supported subset.
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
    REQUIRE(result.score->parts.size() == 1);
    CHECK(result.score->parts.front().name == "Piano");
    CHECK(result.score->parts.front().staffCount == 2);
    CHECK(measureCount(*result.score) == 4);

    engraving::LayoutOptions options;
    options.availableWidth = 400.0;

    const engraving::ScoreLayout layout = engraving::layOutScore(result.score, options);

    REQUIRE(layout.outcome == engraving::LayoutOutcome::laidOut);
    REQUIRE(layout.systems.size() == 1);

    const engraving::System& system = layout.systems.front();

    // Both staves, in one braced system.
    CHECK(system.staffOffsets.size() == 2);
    CHECK(system.staffStartX > 0.0);

    // Both hands present.
    const auto onStaff = [&system](int staff)
    {
        return std::count_if(
            system.noteheads.begin(), system.noteheads.end(),
            [staff](const engraving::NoteheadGlyph& head) { return head.staff == staff; });
    };

    CHECK(onStaff(0) > 0);
    CHECK(onStaff(1) > 0);

    // The lower staff alone changes clef: two clefs on staff 1 (bass, then
    // treble), one on staff 0.
    const auto clefsOn = [&system](int staff)
    {
        return std::count_if(
            system.clefs.begin(), system.clefs.end(),
            [staff](const engraving::ClefGlyph& clef) { return clef.staff == staff; });
    };

    CHECK(clefsOn(0) == 1);
    CHECK(clefsOn(1) == 2);

    // Nothing about a missing staff.
    for (const std::string& notice : layout.notices)
    {
        INFO(notice);
        CHECK(notice.find("staff") == std::string::npos);
    }
}

// The fixture for the engraving-polish surface. Pinned through the importer and
// the layout like the two above, so that it losing the case a later tier is
// checked against fails a build instead of quietly turning that tier's
// before-and-after pictures into pictures of something else.
TEST_CASE("the engraving fixture holds what its picture is about", "[testcontrol][approved][score]")
{
    const MusicXmlImportResult result = importFixture("engraving-polish.musicxml");

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
    REQUIRE(result.score->parts.size() == 1);

    const Part& part = result.score->parts.front();
    CHECK(part.name == "Voice");
    REQUIRE(part.measures.size() == 13);

    const auto eventsOf = [&part](std::size_t measureIndex)
    { return part.measures.at(measureIndex).voices.front().events; };

    const auto sharps = [](const ScoreEvent& event)
    {
        return std::count_if(
            event.notes.begin(), event.notes.end(),
            [](const Note& note) { return note.written.alter == 1; });
    };

    // Bar 1: a triad with a sharp on every note, then a second whose upper,
    // shifted note is the sharp one.
    REQUIRE(eventsOf(0).size() == 2);
    CHECK(eventsOf(0).at(0).notes.size() == 3);
    CHECK(sharps(eventsOf(0).at(0)) == 3);
    CHECK(eventsOf(0).at(1).notes.size() == 2);
    CHECK(sharps(eventsOf(0).at(1)) == 1);

    // Bar 2: two seconds above the middle line, so both take down stems.
    REQUIRE(eventsOf(1).size() == 2);
    CHECK(eventsOf(1).at(0).notes.size() == 2);
    CHECK(eventsOf(1).at(1).notes.size() == 2);

    // Bars 4 to 5: one tie, between them.
    std::size_t ties = 0;

    for (std::size_t index = 0; index < part.measures.size(); ++index)
    {
        for (const ScoreEvent& event : eventsOf(index))
        {
            for (const Note& note : event.notes)
            {
                if (note.tiedTo.has_value())
                {
                    ++ties;
                    CHECK(index == 3);
                }
            }
        }
    }

    CHECK(ties == 1);

    // Bars 6 to 8: a repeat whose first ending is closed by it, then a second.
    CHECK(part.measures.at(5).repeats.repeatStart);
    CHECK(part.measures.at(6).repeats.repeatEnd);
    CHECK(part.measures.at(6).repeats.endingNumbers == std::vector<int>{1});
    CHECK(part.measures.at(7).repeats.endingNumbers == std::vector<int>{2});
    CHECK_FALSE(part.measures.at(7).repeats.repeatEnd);

    // Bars 9 to 12: a whole-bar rest each.
    for (std::size_t index = 8; index < 12; ++index)
    {
        INFO("bar " << index + 1);
        REQUIRE(eventsOf(index).size() == 1);
        CHECK_FALSE(isPitched(eventsOf(index).front()));
    }

    // Bar 3's Segno and bar 13's "D.S. al Coda", read into the model as
    // written, and nothing else anywhere.
    for (std::size_t index = 0; index < part.measures.size(); ++index)
    {
        INFO("bar " << index + 1);

        const std::vector<std::string> expected =
            index == 2    ? std::vector<std::string>{"Segno"}
            : index == 12 ? std::vector<std::string>{"D.S. al Coda"}
                          : std::vector<std::string>{};

        CHECK(part.measures.at(index).repeats.jumps == expected);
    }

    // At about the docked view's width, the first system ends after bar 4, so
    // the tie leaves one system and arrives on the next. That is the case the
    // surface exists to show; a width that no longer breaks there would make
    // the picture of it a picture of an ordinary tie.
    engraving::LayoutOptions options;
    options.availableWidth = 150.0;

    const engraving::ScoreLayout layout = engraving::layOutScore(result.score, options);

    REQUIRE(layout.outcome == engraving::LayoutOutcome::laidOut);
    REQUIRE(layout.systems.size() >= 2);

    CHECK(layout.systems.front().lastMeasureIndex == 3);

    // And the tie is drawn across it, as a half on each system.
    REQUIRE(layout.systems.at(0).ties.size() == 1);
    REQUIRE(layout.systems.at(1).ties.size() == 1);
    CHECK_FALSE(layout.systems.at(0).ties.front().fromSystemStart);
    CHECK(layout.systems.at(1).ties.front().fromSystemStart);

    // Bars 7 and 8, on the second system: "1." closed at the repeat, "2." open.
    const std::vector<engraving::VoltaGlyph>& voltas = layout.systems.at(1).voltas;
    REQUIRE(voltas.size() == 2);
    CHECK(voltas.at(0).label == "1.");
    CHECK(voltas.at(0).hookAtEnd);
    CHECK(voltas.at(1).label == "2.");
    CHECK_FALSE(voltas.at(1).hookAtEnd);

    // Bars 9-12, one multi-measure rest of four.
    std::vector<engraving::MultiRestGlyph> condensed;

    for (const engraving::System& system : layout.systems)
    {
        condensed.insert(condensed.end(), system.multiRests.begin(), system.multiRests.end());
    }

    REQUIRE(condensed.size() == 1);
    CHECK(condensed.front().count == 4);

    // The Segno drawn as its sign, the D.S. as its text.
    std::vector<engraving::JumpGlyph> jumps;

    for (const engraving::System& system : layout.systems)
    {
        jumps.insert(jumps.end(), system.jumps.begin(), system.jumps.end());
    }

    REQUIRE(jumps.size() == 2);
    CHECK(jumps.at(0).sign == engraving::JumpSign::segno);
    CHECK(jumps.at(1).sign == engraving::JumpSign::none);
    CHECK(jumps.at(1).text == "D.S. al Coda");

    // Nothing in the fixture is left unreported or unread.
    for (const std::string& notice : layout.notices)
    {
        INFO(notice);
        CHECK(notice.find("D.C., D.S.") == std::string::npos);
        CHECK(notice.find("endings") == std::string::npos);
        CHECK(notice.find("line of music onto the next") == std::string::npos);
    }
}

// The part-hidden surface names the shipped sample with its voice hidden, by a
// settings payload. Pinned through the codec and the layout against the corpus
// copy of the same song, so a payload that stops hiding the voice -- a renamed
// part, a codec change -- fails a build rather than photographing both parts.
TEST_CASE(
    "the part-hidden state hides the sample's voice",
    "[testcontrol][approved][score][part-selection]")
{
    const auto state = std::find_if(
        approvedWindowStates().begin(), approvedWindowStates().end(),
        [](const ApprovedWindowState& candidate)
        { return candidate.id == "score-view-part-hidden"; });

    REQUIRE(state != approvedWindowStates().end());
    CHECK(state->scoreFixture == bundledSampleFixtureName());

    const score_view::Settings settings =
        score_view::decode({score_view::settingsVersion, state->toolView});

    CHECK(settings.view == score_view::ViewMode::page);
    REQUIRE(settings.hiddenParts.size() == 1);

    const juce::File song =
        juce::File(PRACTICE_TAKES_SOURCE_DIR)
            .getChildFile("src/tests/resources/musicxml/"
                          "mahler-4-die-zwei-blauen-augen-von-meinem-schatz.mxl");
    REQUIRE(song.existsAsFile());

    const MusicXmlImportResult result = importMusicXmlFile(song);
    REQUIRE(result.score != nullptr);

    engraving::LayoutOptions options;
    options.hiddenParts = settings.hiddenParts;

    const engraving::ScoreLayout layout = engraving::layOutScore(result.score, options);

    REQUIRE(layout.outcome == engraving::LayoutOutcome::laidOut);
    CHECK(layout.partName == "Piano");

    // The piano's two staves, braced, and nothing else.
    CHECK(layout.systems.front().staffOffsets.size() == 2);

    // "Alla Marcia" is written in the voice alone, and still reaches the piano.
    const auto tempo = std::find_if(
        layout.systems.front().texts.begin(), layout.systems.front().texts.end(),
        [](const engraving::TextGlyph& text) { return text.role == engraving::TextRole::tempo; });

    REQUIRE(tempo != layout.systems.front().texts.end());
    CHECK(tempo->text.find("Alla Marcia") != std::string::npos);
}
