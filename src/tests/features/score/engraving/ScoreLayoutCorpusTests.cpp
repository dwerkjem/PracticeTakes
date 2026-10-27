#include <catch2/catch_test_macros.hpp>

#include "features/score/engraving/ScoreLayoutEngine.h"

#include "platform/score/musicxml/MusicXmlImporter.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <string>
#include <vector>

// The layout engine against the real-score corpus.
//
// `ScoreLayoutEngineTests` builds its scores by hand, which is what makes each
// rule checkable in isolation and is where every engraving decision is pinned.
// What hand-built scores cannot do is surprise anyone: they contain exactly what
// the case needed. Real exporter output contains clef changes nobody thought
// about, voices numbered 1 and 5, measures that do not add up, tuplets, grace
// notes in chords, and parts with no notes in them.
//
// So this runs the whole corpus through `layOutScore` and asserts the
// properties that must hold whatever is in the file: every glyph is somewhere
// finite, inside the system it belongs to; every system is below the one before
// it; and laying the same score out twice gives the same answer. It pins no
// expectation per file -- `MusicXmlCorpusTests` owns that, against the importer.
using namespace score;
using namespace score::musicxml;

namespace
{
juce::File corpusDirectory()
{
    return juce::File(PRACTICE_TAKES_SOURCE_DIR)
        .getChildFile("src")
        .getChildFile("tests")
        .getChildFile("resources")
        .getChildFile("musicxml");
}

[[nodiscard]] std::vector<juce::File> corpusFiles()
{
    std::vector<juce::File> files;

    for (const juce::File& file :
         corpusDirectory().findChildFiles(juce::File::findFiles, false, "*.mxl"))
    {
        files.push_back(file);
    }

    return files;
}

[[nodiscard]] bool isFinite(double value)
{
    return std::isfinite(value);
}
} // namespace

TEST_CASE("every corpus score lays out", "[score][engraving][corpus]")
{
    const std::vector<juce::File> files = corpusFiles();

    if (files.empty())
    {
        WARN(
            "no corpus score was found, so the layout was checked against none. See "
            << corpusDirectory().getFullPathName() << "/README.md.");

        SUCCEED("corpus empty -- skipped");

        return;
    }

    engraving::LayoutOptions options;
    options.availableWidth = 110.0;

    for (const juce::File& file : files)
    {
        INFO("corpus file: " << file.getFileName());

        const MusicXmlImportResult result = importMusicXmlFile(file);

        REQUIRE(succeeded(result.status));
        REQUIRE(result.score != nullptr);

        const engraving::ScoreLayout layout = engraving::layOutScore(result.score, options);

        // Either it drew something or it said why. A laid-out score with no
        // systems, or a refusal with no notice, would both be the engine
        // failing quietly.
        if (layout.outcome != engraving::LayoutOutcome::laidOut)
        {
            CHECK(layout.systems.empty());
            CHECK_FALSE(layout.notices.empty());

            continue;
        }

        REQUIRE_FALSE(layout.systems.empty());
        CHECK(isFinite(layout.totalHeight));
        CHECK(layout.totalHeight > 0.0);

        double previousTop = -1.0;

        for (const engraving::System& system : layout.systems)
        {
            INFO("system starting at measure " << system.firstMeasureIndex);

            CHECK(isFinite(system.topOfStaff));
            CHECK(system.topOfStaff > previousTop);
            previousTop = system.topOfStaff;

            CHECK(isFinite(system.width));
            CHECK(system.width > 0.0);
            CHECK(system.lastMeasureIndex >= system.firstMeasureIndex);

            // A system always ends in a barline, whatever is or is not in it.
            CHECK_FALSE(system.barlines.empty());

            for (const engraving::NoteheadGlyph& glyph : system.noteheads)
            {
                INFO("notehead at measure " << glyph.source.measureIndex);

                CHECK(isFinite(glyph.x));
                CHECK(glyph.x >= 0.0);
                CHECK(glyph.x <= system.width);

                // Not a bound on legibility -- a piccolo's top note and an
                // organ pedal's bottom note are genuinely far off the staff --
                // but a position outside this range means the clef arithmetic
                // went wrong rather than the music went high.
                CHECK(glyph.position > -60);
                CHECK(glyph.position < 60);

                CHECK(glyph.ledger.above >= 0);
                CHECK(glyph.ledger.below >= 0);
                CHECK_FALSE((glyph.ledger.above > 0 && glyph.ledger.below > 0));

                CHECK(glyph.dots >= 0);
                CHECK(glyph.dots <= 2);
            }

            for (const engraving::StemGlyph& glyph : system.stems)
            {
                CHECK(isFinite(glyph.x));

                // A stem that ends where it starts is a stem nobody can see.
                CHECK(glyph.attachPosition != glyph.tipPosition);
                const bool pointsUp = glyph.tipPosition < glyph.attachPosition;

                CHECK(pointsUp == glyph.up);

                CHECK(glyph.flags >= 0);
                CHECK(glyph.flags <= 5);
            }

            for (const engraving::TieGlyph& glyph : system.ties)
            {
                CHECK(isFinite(glyph.startX));
                CHECK(isFinite(glyph.endX));

                // Both ends are in this system, so neither can be off it.
                CHECK(glyph.startX >= 0.0);
                CHECK(glyph.endX <= system.width);
            }

            for (const engraving::BarlineGlyph& glyph : system.barlines)
            {
                CHECK(isFinite(glyph.x));
                CHECK(glyph.x >= 0.0);
                CHECK(glyph.x <= system.width);
            }
        }
    }
}

// Determinism, against files nobody wrote for the test. The hand-built case in
// ScoreLayoutEngineTests makes the same claim; this is the one that would catch
// an ordering that depends on a map's iteration order or on a pointer value,
// which a three-note score never would.
TEST_CASE("a corpus score lays out the same way twice", "[score][engraving][corpus]")
{
    const std::vector<juce::File> files = corpusFiles();

    if (files.empty())
    {
        SUCCEED("corpus empty -- skipped");

        return;
    }

    engraving::LayoutOptions options;
    options.availableWidth = 96.0;

    for (const juce::File& file : files)
    {
        INFO("corpus file: " << file.getFileName());

        const MusicXmlImportResult result = importMusicXmlFile(file);

        REQUIRE(result.score != nullptr);

        const engraving::ScoreLayout first = engraving::layOutScore(result.score, options);
        const engraving::ScoreLayout second = engraving::layOutScore(result.score, options);

        REQUIRE(first.systems.size() == second.systems.size());
        CHECK(first.notices == second.notices);
        CHECK(first.lyricVerseCount == second.lyricVerseCount);

        for (std::size_t index = 0; index < first.systems.size(); ++index)
        {
            const engraving::System& left = first.systems.at(index);
            const engraving::System& right = second.systems.at(index);

            REQUIRE(left.noteheads.size() == right.noteheads.size());
            CHECK(left.width == right.width);

            for (std::size_t head = 0; head < left.noteheads.size(); ++head)
            {
                CHECK(left.noteheads.at(head).x == right.noteheads.at(head).x);
                CHECK(left.noteheads.at(head).position == right.noteheads.at(head).position);
                CHECK(left.noteheads.at(head).accidental == right.noteheads.at(head).accidental);
            }
        }
    }
}
