#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <map>
#include <string>
#include <vector>

#include "platform/score/musicxml/MusicXmlImporter.h"

// Real scores, imported end to end.
//
// The synthetic fixtures elsewhere in this tree only contain the bugs someone
// already thought of. Real exporter output is what contains the rest, and it
// delivered: every non-MuseScore dialect here found a defect on arrival --
// unpitched chord tones advancing the voice cursor, ties flattened from
// per-note to per-chord, and bars trusting a mensuration sign over their own
// content. None of the three was reachable from a hand-written fixture.
//
// So the assertions are deliberately coarse -- part count, measure count,
// musical length, diagnostic count -- and pinned in `expectations.txt`, so that
// a change in how a real file converts fails rather than passing silently.
//
// WHAT IS AND IS NOT COVERED
//
// Thirty-six scores, from two sources with licences checked per file:
// twenty CC0 songs from the OpenScore Lieder Corpus (voice and piano), and
// sixteen Public Domain or CC-BY choral works from CPDL.
//
// Covered: six exporters (MuseScore 2/3, Finale v25-v27, Sibelius 7-22,
// Dorico 5, Harmony Assistant, PDFtoMusic Pro); one to eleven parts; SATB in
// open score and in closed score, the latter being two staves carrying two
// voices each through <backup>; Renaissance polyphony through early
// modernism.
//
// **Not** covered, so nobody reads "real-score coverage" as more than it is:
//
//  - **No percussion.** CPDL is a choral library and has none, and the
//    public-domain MuseScore-derived datasets rest on uploader
//    self-declaration this project does not accept. MusicXmlPercussionTests
//    covers the idiom against a fixture reproduced from a real export instead.
//  - **No orchestral score** mixing transposing winds, percussion, and strings
//    in one file.
//  - **No uncompressed document.** Every corpus file is a `.mxl` container;
//    plain `.musicxml` and `.xml` input is covered only by the fixtures.
using namespace score;
using namespace score::musicxml;

namespace
{
struct Expectation
{
    int parts = 0;
    int measures = 0;
    long long totalTicks = 0;
    int diagnostics = 0;
};

juce::File corpusDirectory()
{
    return juce::File(PRACTICE_TAKES_TEST_RESOURCES_DIR).getChildFile("musicxml");
}

std::vector<juce::File> corpusFiles()
{
    std::vector<juce::File> files;

    for (const juce::File& candidate :
         corpusDirectory().findChildFiles(juce::File::findFiles, false))
    {
        const juce::String extension = candidate.getFileExtension().toLowerCase();

        if (extension == ".musicxml" || extension == ".xml" || extension == ".mxl")
        {
            files.push_back(candidate);
        }
    }

    return files;
}

std::map<std::string, Expectation> readExpectations()
{
    std::map<std::string, Expectation> expectations;
    const juce::File manifest = corpusDirectory().getChildFile("expectations.txt");

    if (!manifest.existsAsFile())
    {
        return expectations;
    }

    juce::StringArray lines;
    manifest.readLines(lines);

    for (const juce::String& line : lines)
    {
        const juce::String trimmed = line.trim();

        if (trimmed.isEmpty() || trimmed.startsWithChar('#'))
        {
            continue;
        }

        juce::StringArray fields;
        fields.addTokens(trimmed, "|", "");
        fields.trim();

        if (fields.size() != 5)
        {
            FAIL(
                "expectations.txt has a row with "
                << fields.size() << " fields instead of 5: " << trimmed);

            continue;
        }

        Expectation expectation;
        expectation.parts = fields[1].getIntValue();
        expectation.measures = fields[2].getIntValue();
        expectation.totalTicks = fields[3].getLargeIntValue();
        expectation.diagnostics = fields[4].getIntValue();

        expectations.emplace(fields[0].toStdString(), expectation);
    }

    return expectations;
}
} // namespace

TEST_CASE(
    "the real-score corpus imports as it did when each file was added",
    "[score][musicxml][corpus]")
{
    // **expectations.txt is the corpus manifest.** A score is in the corpus if
    // and only if it has a row there, and this iterates the manifest rather
    // than the directory.
    //
    // That distinction matters because the directory may also hold scores that
    // are useful to test against locally and cannot be committed -- the
    // repertoire is under copyright and this is a public repository. Those are
    // gitignored, exercised by the invariants case below, and reported as
    // extras here. They must not fail a build that does not have them.
    const std::map<std::string, Expectation> expectations = readExpectations();

    if (expectations.empty())
    {
        // Reported rather than passed over in silence: an empty corpus is a gap
        // in the safety net, not a clean result. WARN prints even when the
        // suite passes, so the gap stays visible in every run.
        WARN(
            "expectations.txt lists no scores, so no real score was checked. See "
            << corpusDirectory().getFullPathName() << "/README.md.");

        SUCCEED("corpus manifest empty -- skipped");

        return;
    }

    for (const auto& [name, expectation] : expectations)
    {
        INFO("corpus file: " << name);

        const juce::File file = corpusDirectory().getChildFile(juce::String(name));

        // A row naming a file that is not there is a broken corpus, not a
        // missing optional extra.
        REQUIRE(file.existsAsFile());

        const MusicXmlImportResult result = importMusicXmlFile(file);

        REQUIRE(succeeded(result.status));
        REQUIRE(result.score != nullptr);

        CHECK(static_cast<int>(result.score->parts.size()) == expectation.parts);
        CHECK(static_cast<int>(measureCount(*result.score)) == expectation.measures);
        CHECK(static_cast<long long>(totalLength(*result.score)) == expectation.totalTicks);
        CHECK(static_cast<int>(result.diagnostics.size()) == expectation.diagnostics);
    }

    // Anything on disk the manifest does not name. Not a failure -- see above --
    // but worth saying, so a file added and never pinned does not sit there
    // looking like coverage it is not providing.
    for (const juce::File& file : corpusFiles())
    {
        const std::string name = file.getFileName().toStdString();

        if (expectations.count(name) > 0)
        {
            continue;
        }

        WARN(
            "\"" << name
                 << "\" is in the corpus directory but not in expectations.txt, so nothing is "
                    "pinned about it. Run \"[.corpus-report]\" to get its row.");
    }
}

TEST_CASE("report what every corpus file imports to", "[.corpus-report]")
{
    // Hidden, opt-in, and printed rather than asserted -- run it with
    //
    //     PracticeTakesTests "[.corpus-report]" --success
    //
    // This is the tool the resources README points at for step 3 of adding a
    // corpus file: it prints the row to paste into expectations.txt, and the
    // diagnostic breakdown that says whether the number in that row is a
    // healthy import or a warning sign.
    //
    // It asserts almost nothing on purpose. The pinned expectations are what
    // catch a regression; this is for the human deciding what to pin.
    for (const juce::File& file : corpusFiles())
    {
        const MusicXmlImportResult result = importMusicXmlFile(file);
        const std::string name = file.getFileName().toStdString();

        if (!succeeded(result.status))
        {
            WARN(name << "\n  FAILED: " << result.error);

            continue;
        }

        const Score& score = *result.score;
        const int measures = static_cast<int>(measureCount(score));
        const long long ticks = static_cast<long long>(totalLength(score));

        // Counted by element name so a large diagnostic total can be read as
        // "one construct this importer drops, used everywhere" rather than
        // "hundreds of separate problems".
        std::map<std::string, int> byElement;
        int unsupported = 0;
        int repaired = 0;

        for (const Diagnostic& diagnostic : result.diagnostics)
        {
            byElement[diagnostic.elementName] += diagnostic.occurrences;

            if (diagnostic.severity == DiagnosticSeverity::unsupported)
            {
                ++unsupported;
            }
            else if (diagnostic.severity == DiagnosticSeverity::repaired)
            {
                ++repaired;
            }
        }

        std::string report =
            name + "\n  expectations.txt row:\n    " + name + " | " +
            std::to_string(score.parts.size()) + " | " + std::to_string(measures) + " | " +
            std::to_string(ticks) + " | " + std::to_string(result.diagnostics.size()) + "\n";

        report +=
            "  " + std::to_string(ticks / (ticksPerQuarterNote * 4)) + " whole notes of music, " +
            std::to_string(score.tempoMap.entries().size()) + " tempo entries, first " +
            std::to_string(static_cast<int>(score.tempoMap.beatsPerMinuteAt(0))) + " bpm\n";
        report += "  written by: " +
                  (score.metadata.encodingSoftware.empty() ? std::string{"(not stated)"}
                                                           : score.metadata.encodingSoftware) +
                  "\n";
        report += "  diagnostics: " + std::to_string(result.diagnostics.size()) + " (" +
                  std::to_string(unsupported) + " unsupported, " + std::to_string(repaired) +
                  " repaired)\n";

        for (const auto& [element, count] : byElement)
        {
            report += "    " + element + " x" + std::to_string(count) + "\n";
        }

        WARN(report);
    }
}

TEST_CASE("every corpus score satisfies the model's invariants", "[score][musicxml][corpus]")
{
    // Every score on disk, pinned or not. The invariants are the model's promise
    // to its consumers and hold for any score whatever, so an uncommitted local
    // file tests them just as well -- and this is where the copyrighted scores
    // that cannot be committed still earn their keep.
    const std::vector<juce::File> files = corpusFiles();

    if (files.empty())
    {
        SUCCEED("no corpus scores on disk -- skipped");

        return;
    }

    for (const juce::File& file : files)
    {
        INFO("corpus file: " << file.getFileName().toStdString());

        const MusicXmlImportResult result = importMusicXmlFile(file);
        REQUIRE(succeeded(result.status));

        const Score& score = *result.score;

        // Invariant 1: every part agrees on the measure grid.
        for (const Part& part : score.parts)
        {
            CHECK(part.measures.size() == measureCount(score));
            CHECK_FALSE(part.id.empty());

            for (std::size_t index = 0; index < part.measures.size(); ++index)
            {
                const Measure& measure = part.measures[index];

                CHECK(measure.index == index);
                CHECK(measure.start == score.parts.front().measures[index].start);
                CHECK(
                    measure.nominalDuration == score.parts.front().measures[index].nominalDuration);

                for (const Voice& voice : measure.voices)
                {
                    Tick previousEnd = 0;

                    for (const ScoreEvent& event : voice.events)
                    {
                        // Invariants 2, 3, and 7.
                        CHECK(event.duration >= 0);
                        CHECK(event.onset >= previousEnd);
                        CHECK(event.onset + event.duration <= measure.nominalDuration);

                        if (event.duration == 0)
                        {
                            CHECK(event.isGrace);
                        }

                        // Invariant 5: spelling and sounding pitch agree.
                        for (const Note& note : event.notes)
                        {
                            CHECK(isConsistent(note.pitch));
                        }

                        previousEnd = event.onset + event.duration;
                    }
                }
            }
        }

        // Invariant 8: the tempo map is never empty.
        CHECK_FALSE(score.tempoMap.entries().empty());
    }
}
