#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "platform/score/musicxml/MusicXmlImporter.h"
#include "support/MusicXmlFixtures.h"

// Lyrics -- the piece a singer-facing practice application exists for.
//
// A syllable is modelled properly rather than as a bare string because the
// renderer needs to know whether to draw a hyphen after it and whether to draw
// an extender line under a melisma, and neither is recoverable from the text.
// These cases are what stop a later change from quietly reducing it to text.
using namespace score;
using namespace score::musicxml;
using namespace testing::musicxml;

namespace
{
std::shared_ptr<const Score> importOrFail(const std::string& document)
{
    const MusicXmlImportResult result = importMusicXmlDocument(document);

    REQUIRE(succeeded(result.status));
    REQUIRE(result.score != nullptr);

    return result.score;
}

std::string lyric(
    const std::string& text,
    const std::string& syllabic = "single",
    int verse = 1,
    bool extend = false)
{
    return "        <lyric number=\"" + std::to_string(verse) + "\">\n          <syllabic>" +
           syllabic + "</syllabic>\n          <text>" + text + "</text>\n" +
           (extend ? "          <extend/>\n" : "") + "        </lyric>\n";
}

const std::vector<LyricSyllable>&
lyricsOf(const Score& score, std::size_t measureIndex, std::size_t eventIndex)
{
    return score.parts.front().measures[measureIndex].voices.front().events[eventIndex].lyrics;
}
} // namespace

TEST_CASE("a syllable keeps its text and its position within the word", "[score][musicxml][lyric]")
{
    // "A-ve ve-rum": the hyphens are not in the text, they are implied by the
    // syllabic position, and only the position tells the renderer to draw them.
    const std::string body = measure(
        "1", attributes(1, 4, 4) + note("C", 5, 1, 1, lyric("A", "begin")) +
                 note("D", 5, 1, 1, lyric("ve", "end")) + note("E", 5, 1, 1, lyric("ve", "begin")) +
                 note("F", 5, 1, 1, lyric("rum", "end")));

    const auto score = importOrFail(scoreDocument(body));

    REQUIRE(lyricsOf(*score, 0, 0).size() == 1);
    CHECK(lyricsOf(*score, 0, 0).front().text == "A");
    CHECK(lyricsOf(*score, 0, 0).front().position == SyllabicPosition::begin);

    CHECK(lyricsOf(*score, 0, 1).front().text == "ve");
    CHECK(lyricsOf(*score, 0, 1).front().position == SyllabicPosition::end);

    CHECK(lyricsOf(*score, 0, 3).front().text == "rum");
    CHECK(lyricsOf(*score, 0, 3).front().position == SyllabicPosition::end);
}

TEST_CASE("multiple verses attach to the same note", "[score][musicxml][lyric]")
{
    // A hymn with four verses has four syllables on most notes. They are
    // separate verses, not a sequence of syllables, which is why the verse
    // number is kept rather than the order relied on.
    const std::string fourVerses = lyric("Praise", "single", 1) + lyric("Bless", "single", 2) +
                                   lyric("Sing", "single", 3) + lyric("Come", "single", 4);
    const std::string body = measure("1", attributes(1, 4, 4) + note("C", 5, 4, 1, fourVerses));

    const auto score = importOrFail(scoreDocument(body));
    const std::vector<LyricSyllable>& syllables = lyricsOf(*score, 0, 0);

    REQUIRE(syllables.size() == 4);
    CHECK(syllables[0].verse == 1);
    CHECK(syllables[0].text == "Praise");
    CHECK(syllables[3].verse == 4);
    CHECK(syllables[3].text == "Come");
}

TEST_CASE("a melisma carries its extend flag", "[score][musicxml][lyric]")
{
    // One syllable held across several notes. The renderer draws an extender
    // line; nothing else in the MVP interprets it, but losing the flag makes
    // the line impossible to draw later.
    const std::string body = measure(
        "1", attributes(1, 4, 4) + note("C", 5, 2, 1, lyric("Glo", "begin", 1, /*extend=*/true)) +
                 note("D", 5, 1) + note("E", 5, 1, 1, lyric("ri-a", "end")));

    const auto score = importOrFail(scoreDocument(body));

    REQUIRE(lyricsOf(*score, 0, 0).size() == 1);
    CHECK(lyricsOf(*score, 0, 0).front().extend);

    // The notes the melisma is held across carry no syllable of their own.
    CHECK(lyricsOf(*score, 0, 1).empty());

    CHECK_FALSE(lyricsOf(*score, 0, 2).front().extend);
}

TEST_CASE("an extend with no text is kept as a continuation", "[score][musicxml][lyric]")
{
    // Exporters write a bare <extend/> on the notes a melisma continues over.
    // It has no text and is still worth keeping; a syllable with neither text
    // nor extender says nothing and is dropped.
    const std::string bareExtend = "        <lyric number=\"1\"><extend/></lyric>\n";
    const std::string emptyLyric = "        <lyric number=\"1\"><text></text></lyric>\n";
    const std::string body = measure(
        "1", attributes(1, 4, 4) + note("C", 5, 2, 1, bareExtend) + note("D", 5, 2, 1, emptyLyric));

    const auto score = importOrFail(scoreDocument(body));

    REQUIRE(lyricsOf(*score, 0, 0).size() == 1);
    CHECK(lyricsOf(*score, 0, 0).front().extend);
    CHECK(lyricsOf(*score, 0, 0).front().text.empty());

    CHECK(lyricsOf(*score, 0, 1).empty());
}

TEST_CASE(
    "a syllable on a chord attaches to the chord, not to each note",
    "[score][musicxml][lyric]")
{
    // MusicXML writes the lyric on the first note of the chord. The chord is
    // one event, so the syllable lands on it once -- not once per pitch.
    const std::string chordExtra = "        <chord/>\n";
    const std::string body = measure(
        "1", attributes(1, 4, 4) + note("C", 4, 4, 1, lyric("Sing")) +
                 note("E", 4, 4, 1, chordExtra) + note("G", 4, 4, 1, chordExtra));

    const auto score = importOrFail(scoreDocument(body));
    const Voice& voice = score->parts.front().measures.front().voices.front();

    REQUIRE(voice.events.size() == 1);
    CHECK(voice.events.front().notes.size() == 3);

    REQUIRE(voice.events.front().lyrics.size() == 1);
    CHECK(voice.events.front().lyrics.front().text == "Sing");
}

TEST_CASE("a syllable with no verse number is verse one", "[score][musicxml][lyric]")
{
    const std::string unnumbered =
        "        <lyric><syllabic>single</syllabic><text>Ah</text></lyric>\n";
    const std::string body = measure("1", attributes(1, 4, 4) + note("C", 5, 4, 1, unnumbered));

    const auto score = importOrFail(scoreDocument(body));

    REQUIRE(lyricsOf(*score, 0, 0).size() == 1);
    CHECK(lyricsOf(*score, 0, 0).front().verse == 1);
    CHECK(lyricsOf(*score, 0, 0).front().text == "Ah");
}

TEST_CASE("a rest carries no lyrics", "[score][musicxml][lyric]")
{
    const std::string body =
        measure("1", attributes(1, 4, 4) + note("C", 5, 2, 1, lyric("Sing")) + rest(2));

    const auto score = importOrFail(scoreDocument(body));

    CHECK(lyricsOf(*score, 0, 0).size() == 1);
    CHECK(lyricsOf(*score, 0, 1).empty());
}
