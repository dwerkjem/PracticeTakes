#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <map>
#include <string>
#include <vector>

#include "platform/score/musicxml/MusicXmlImporter.h"

// Real scores, imported end to end.
//
// ============================================================================
//  THIS CORPUS IS MUSESCORE EXPORTS ONLY.
// ============================================================================
//
// Read that before reading anything below as "real-score coverage". No second
// notation program was available when this was written, so every file here
// comes from one exporter and this suite proves nothing about the others.
// Finale emits <divisions> values and voice numbering unlike MuseScore's, and
// Sibelius emits far more layout elements; neither is exercised. A Finale,
// Sibelius, or Dorico export would be a cheap and worthwhile addition, and
// there is a follow-up issue for it.
//
// What this suite is for, given that: the synthetic fixtures elsewhere in this
// tree only contain the bugs we already thought of. Real exporter output is
// what contains the rest. So these assertions are deliberately coarse -- part
// count, measure count, musical length, diagnostic count -- and pinned in
// `expectations.txt`, so that a change in how a real file converts fails rather
// than passing silently.
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
    const std::vector<juce::File> files = corpusFiles();

    if (files.empty())
    {
        // Reported rather than passed over in silence: an empty corpus is a gap
        // in the safety net, not a clean result. `WARN` prints even when the
        // suite passes, so the gap stays visible in every test run.
        WARN(
            "The MusicXML corpus at "
            << corpusDirectory().getFullPathName()
            << " is empty, so no real score was imported. "
               "See its README.md for how to add one.");

        SUCCEED("corpus empty -- skipped");

        return;
    }

    const std::map<std::string, Expectation> expectations = readExpectations();

    for (const juce::File& file : files)
    {
        const std::string name = file.getFileName().toStdString();

        INFO("corpus file: " << name);

        const MusicXmlImportResult result = importMusicXmlFile(file);

        REQUIRE(succeeded(result.status));
        REQUIRE(result.score != nullptr);

        const int parts = static_cast<int>(result.score->parts.size());
        const int measures = static_cast<int>(measureCount(*result.score));
        const long long ticks = static_cast<long long>(totalLength(*result.score));
        const int diagnostics = static_cast<int>(result.diagnostics.size());

        const auto expectation = expectations.find(name);

        if (expectation == expectations.end())
        {
            // A corpus file nobody asserts anything about cannot catch a
            // regression, so an unpinned file is a failure rather than a pass.
            // The values it actually produced are printed so the row can be
            // written without hunting for them.
            FAIL(
                "No row in expectations.txt for \""
                << name << "\". Add this line:\n  " << name << " | " << parts << " | " << measures
                << " | " << ticks << " | " << diagnostics);

            continue;
        }

        CHECK(parts == expectation->second.parts);
        CHECK(measures == expectation->second.measures);
        CHECK(ticks == expectation->second.totalTicks);
        CHECK(diagnostics == expectation->second.diagnostics);
    }
}

TEST_CASE("every corpus score satisfies the model's invariants", "[score][musicxml][corpus]")
{
    const std::vector<juce::File> files = corpusFiles();

    if (files.empty())
    {
        SUCCEED("corpus empty -- skipped");

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
                        for (const Pitch& pitch : event.pitches)
                        {
                            CHECK(isConsistent(pitch));
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
