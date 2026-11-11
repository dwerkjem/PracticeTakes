#include <catch2/catch_test_macros.hpp>

#include <string>

#include "PracticeTakesSamples.h"
#include "application/shell/ui/score/BundledSampleScore.h"
#include "application/testcontrol/SyntheticTake.h"
#include "platform/library/LibraryListing.h"
#include "platform/library/TakeFiles.h"
#include "platform/score/musicxml/MusicXmlImporter.h"
#include "support/MusicXmlFixtures.h"

// What the Library window lists, read from a temporary library.
namespace
{
class ScratchFolder
{
  public:
    ScratchFolder()
        : folder(juce::File::getSpecialLocation(juce::File::tempDirectory)
                     .getNonexistentChildFile("practice-takes-listing-test", ""))
    {
        static_cast<void>(folder.createDirectory());
    }

    ~ScratchFolder()
    {
        folder.deleteRecursively();
    }

    ScratchFolder(const ScratchFolder&) = delete;
    ScratchFolder& operator=(const ScratchFolder&) = delete;

    [[nodiscard]] const juce::File& get() const
    {
        return folder;
    }

  private:
    juce::File folder;
};

// A one-bar score titled `title`, by `composer`.
std::string titled(const std::string& title, const std::string& composer)
{
    using namespace testing::musicxml;
    std::string document = scoreDocument(
        "    <measure number=\"1\">\n" + attributes(1, 4, 4) + note("C", 4, 4) + "    </measure>\n",
        "Flute");
    const std::string head = "<score-partwise version=\"4.0\">";
    document.replace(
        document.find(head), head.size(),
        head + "\n  <work><work-title>" + title +
            "</work-title></work>\n  <identification><creator type=\"composer\">" + composer +
            "</creator></identification>");
    return document;
}

juce::File written(const juce::File& file, const std::string& text)
{
    static_cast<void>(file.getParentDirectory().createDirectory());
    REQUIRE(file.replaceWithText(juce::String(text)));
    return file;
}

juce::File installSample(const juce::File& folder)
{
    return bundled_sample::install(
        folder, PracticeTakesSamples::mahlerdiezweiblauenaugen_mxl,
        static_cast<std::size_t>(PracticeTakesSamples::mahlerdiezweiblauenaugen_mxlSize));
}
} // namespace

TEST_CASE("songs are listed with what they are, the sample first", "[library][listing]")
{
    const ScratchFolder scratch;
    const library::Layout layout{scratch.get().getChildFile("Library")};
    const juce::File samples = scratch.get().getChildFile("Samples");
    REQUIRE(installSample(samples) != juce::File());

    // An empty library still lists the sample, and cannot remove it.
    auto songs = library::listSongs(layout, samples);
    REQUIRE(songs.size() == 1);
    CHECK(songs[0].builtIn);
    CHECK(songs[0].readable);
    CHECK_FALSE(songs[0].parts.empty());

    written(layout.songs().getChildFile("b.musicxml"), titled("Sonata", "Haydn"));
    written(layout.songs().getChildFile("a.musicxml"), titled("Air", "Bach"));
    written(layout.songs().getChildFile("broken.musicxml"), "<not-a-score/>");
    written(layout.songs().getChildFile("notes.txt"), "not a score at all");

    songs = library::listSongs(layout, samples);
    REQUIRE(songs.size() == 4);
    CHECK(songs[0].builtIn);
    CHECK(songs[1].title == "Air");
    CHECK(songs[1].composer == "Bach");
    REQUIRE(songs[1].parts.size() == 1);
    CHECK(songs[1].parts[0] == "Flute");
    CHECK_FALSE(songs[1].builtIn);
    // A file that does not import is listed by its name, marked.
    CHECK(songs[2].title == "broken");
    CHECK_FALSE(songs[2].readable);
    CHECK(songs[3].title == "Sonata");
}

TEST_CASE("the song index is a cache, and a changed file is read again", "[library][listing]")
{
    const ScratchFolder scratch;
    const library::Layout layout{scratch.get().getChildFile("Library")};
    const juce::File song =
        written(layout.songs().getChildFile("a.musicxml"), titled("Air", "Bach"));

    CHECK(library::listSongs(layout, {})[0].title == "Air");
    const juce::File index = layout.songs().getChildFile(library::songIndexFileName);
    REQUIRE(index.existsAsFile());

    // Listed from the cache while the file's name, size and time match: the
    // cache says something different, and the listing believes it.
    index.replaceWithText(index.loadFileAsString().replace("\"Air\"", "\"Cached\""));
    CHECK(library::listSongs(layout, {})[0].title == "Cached");

    // A changed file is read again.
    written(song, titled("Air on the G string", "Bach"));
    REQUIRE(song.setLastModificationTime(
        juce::Time::getCurrentTime() + juce::RelativeTime::seconds(5)));
    CHECK(library::listSongs(layout, {})[0].title == "Air on the G string");

    // A cache that does not read is rebuilt.
    index.replaceWithText("{ broken");
    CHECK(library::listSongs(layout, {})[0].title == "Air on the G string");
    CHECK(index.loadFileAsString().contains("Air on the G string"));
}

TEST_CASE("takes are listed newest first, and a half-saved one not at all", "[library][listing]")
{
    const ScratchFolder scratch;
    const library::Layout layout{scratch.get().getChildFile("Library")};
    const juce::File song = installSample(layout.songs());
    const auto imported = score::musicxml::importMusicXmlFile(song);
    REQUIRE(imported.score != nullptr);
    const take::Take played = testcontrol::syntheticKeyboardTake(imported.score, 1, 2);

    REQUIRE(library::writeTake(layout.takes().getChildFile("older"), played, song, 1000).empty());
    REQUIRE(library::writeTake(layout.takes().getChildFile("newer"), played, song, 2000).empty());
    // A save cut short: no manifest.
    REQUIRE(layout.takes().getChildFile("partial").createDirectory());
    // A take from a newer application: listed, and says so.
    written(
        layout.takes().getChildFile("future").getChildFile(library::manifestFileName),
        R"({"formatVersion": "5.0"})");

    const auto takes = library::listTakes(layout);
    REQUIRE(takes.size() == 3);
    // The unreadable one is dated by its folder: now, so first.
    CHECK(takes[0].folder.getFileName() == "future");
    CHECK(takes[0].error == "library.take.error.newer");
    CHECK(takes[1].folder.getFileName() == "newer");
    REQUIRE(takes[1].manifest.has_value());
    CHECK(takes[1].manifest->scoreTitle == played.scoreTitle);
    CHECK(takes[1].bytes > 0);
    CHECK(takes[2].folder.getFileName() == "older");

    CHECK(library::listTakes(library::Layout{scratch.get().getChildFile("Nowhere")}).empty());
}
