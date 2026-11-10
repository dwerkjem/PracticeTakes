#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>

#include "PracticeTakesSamples.h"
#include "application/shell/ui/score/BundledSampleScore.h"
#include "platform/score/musicxml/MusicXmlImporter.h"
#include "platform/search/ScoreSearch.h"

// The open score's searchable lines and bars, read from the sample: a song
// with lyrics in German, split into syllables across bars.
namespace
{
std::shared_ptr<const score::Score> sample()
{
    const juce::File folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                  .getNonexistentChildFile("practice-takes-score-search", "");
    const juce::File file = bundled_sample::install(
        folder, PracticeTakesSamples::mahlerdiezweiblauenaugen_mxl,
        static_cast<std::size_t>(PracticeTakesSamples::mahlerdiezweiblauenaugen_mxlSize));
    auto imported = score::musicxml::importMusicXmlFile(file);
    folder.deleteRecursively();
    REQUIRE(imported.score != nullptr);
    return imported.score;
}
} // namespace

TEST_CASE("a score's lyrics are searched phrase by phrase, words whole", "[search][score]")
{
    const auto song = sample();
    const auto phrases = search::lyricPhrasesOf(*song);
    REQUIRE(phrases.size() > 5);

    std::string all;
    for (const auto& phrase : phrases)
    {
        // Only the voice sings, and every phrase is placed.
        CHECK(phrase.partIndex == 0);
        CHECK_FALSE(phrase.bar.empty());
        // Syllables are joined into words: no hyphen survives, and no word
        // starts or ends with a space.
        CHECK(phrase.text.find('-') == std::string::npos);
        CHECK(phrase.text.front() != ' ');
        CHECK(phrase.text.back() != ' ');
        all += phrase.text + " | ";
    }
    INFO(all);
    // The title's words are sung, as whole words.
    CHECK(all.find("blau") != std::string::npos);
    CHECK(all.find("Augen") != std::string::npos);

    // In time order.
    for (std::size_t index = 1; index < phrases.size(); ++index)
    {
        CHECK(phrases[index - 1].tick < phrases[index].tick);
    }
}

TEST_CASE("every bar of a score can be gone to", "[search][score]")
{
    const auto song = sample();
    const auto bars = search::barsOf(*song);
    REQUIRE(bars.size() == song->parts.front().measures.size());
    CHECK(bars.front().tick == 0);
    for (std::size_t index = 1; index < bars.size(); ++index)
    {
        CHECK(bars[index].tick > bars[index - 1].tick);
        CHECK_FALSE(bars[index].bar.empty());
    }
    CHECK(search::barsOf(score::Score{}).empty());
    CHECK(search::lyricPhrasesOf(score::Score{}).empty());
}
