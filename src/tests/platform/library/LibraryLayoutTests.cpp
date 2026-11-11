#include <catch2/catch_test_macros.hpp>

#include "platform/library/LibraryLayout.h"

// Where the library puts things. Every test runs against a fresh temporary
// folder, so none sees the user's library or another test's.
namespace
{
class ScratchFolder
{
  public:
    ScratchFolder()
        : folder(juce::File::getSpecialLocation(juce::File::tempDirectory)
                     .getNonexistentChildFile("practice-takes-library-test", ""))
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

juce::File written(const juce::File& file, const juce::String& text)
{
    static_cast<void>(file.getParentDirectory().createDirectory());
    REQUIRE(file.replaceWithText(text));
    return file;
}
} // namespace

TEST_CASE("the library keeps songs and takes in two folders", "[library]")
{
    const ScratchFolder scratch;
    const library::Layout layout{scratch.get().getChildFile("Library")};
    CHECK(layout.songs() == scratch.get().getChildFile("Library").getChildFile("Songs"));
    CHECK(layout.takes() == scratch.get().getChildFile("Library").getChildFile("Takes"));
    CHECK(layout.contains(layout.songs().getChildFile("a.musicxml")));
    CHECK_FALSE(layout.contains(scratch.get().getChildFile("a.musicxml")));
    CHECK_FALSE(library::Layout{}.contains(scratch.get()));
    CHECK(library::defaultFolder().getFileName() == "Practice Takes");
}

TEST_CASE("a take's folder is named by its time and song", "[library]")
{
    const juce::Time when(2026, 10, 10, 21, 4, 9, 0, true);
    CHECK(
        library::takeFolderName(when, "Die zwei blauen Augen") ==
        "2026-11-10 21-04-09 Die zwei blauen Augen");
    CHECK(library::takeFolderName(when, "  ") == "2026-11-10 21-04-09");
    // Nothing a file system refuses, and nothing too long.
    const juce::String name =
        library::takeFolderName(when, "A/B: C?" + juce::String::repeatedString("x", 200));
    CHECK_FALSE(name.containsAnyOf("/\\:?"));
    CHECK(name.length() <= 80);

    const ScratchFolder scratch;
    const juce::File first = library::newTakeFolder(scratch.get(), when, "Song");
    CHECK(first.getFileName() == "2026-11-10 21-04-09 Song");
    REQUIRE(first.createDirectory());
    CHECK(
        library::newTakeFolder(scratch.get(), when, "Song").getFileName() ==
        "2026-11-10 21-04-09 Song (2)");
}

TEST_CASE("opening a score from elsewhere copies it in once", "[library]")
{
    const ScratchFolder scratch;
    const library::Layout layout{scratch.get().getChildFile("Library")};
    const juce::File downloaded =
        written(scratch.get().getChildFile("Downloads/Aria.musicxml"), "one");

    const juce::File copy = library::intoLibrary(layout, downloaded);
    CHECK(copy == layout.songs().getChildFile("Aria.musicxml"));
    CHECK(copy.loadFileAsString() == "one");

    // The same file again is the same song.
    CHECK(library::intoLibrary(layout, downloaded) == copy);
    // The same bytes under another name are too.
    const juce::File renamed = written(scratch.get().getChildFile("Other/Renamed.musicxml"), "one");
    CHECK(library::intoLibrary(layout, renamed) == copy);

    // A different score with the same name gets a numbered one.
    const juce::File different =
        written(scratch.get().getChildFile("Elsewhere/Aria.musicxml"), "two");
    const juce::File second = library::intoLibrary(layout, different);
    CHECK(second == layout.songs().getChildFile("Aria (2).musicxml"));
    CHECK(second.loadFileAsString() == "two");

    // A score already in the library opens where it is.
    CHECK(library::intoLibrary(layout, second) == second);
}

TEST_CASE("a library that cannot be written gives no copy", "[library]")
{
    const ScratchFolder scratch;
    // The library folder's place is taken by a file, so no Songs folder can be
    // made in it.
    const juce::File blocked = written(scratch.get().getChildFile("Library"), "not a folder");
    const juce::File score = written(scratch.get().getChildFile("Aria.musicxml"), "one");
    CHECK(library::intoLibrary(library::Layout{blocked}, score) == juce::File());
}

TEST_CASE("the bin keeps what was removed, and where it came from", "[library]")
{
    const ScratchFolder scratch;
    const juce::File trash = scratch.get().getChildFile("Trash");
    const juce::Time when(2026, 10, 10, 21, 4, 9, 0, true);

    const juce::File take = scratch.get().getChildFile("Takes/2026-11-10 21-04-09 Song");
    written(take.getChildFile("take.json"), "{}");
    REQUIRE(library::moveToTrashIn(take, trash, when));
    CHECK_FALSE(take.exists());
    CHECK(trash.getChildFile("files/2026-11-10 21-04-09 Song/take.json").existsAsFile());
    const juce::String record =
        trash.getChildFile("info/2026-11-10 21-04-09 Song.trashinfo").loadFileAsString();
    CHECK(record.startsWith("[Trash Info]\n"));
    // The path as a URL path: escaped, with no space left in it, and reading
    // back as the item's own path -- whatever the platform writes paths with.
    const juce::String path = record.fromFirstOccurrenceOf("Path=", false, false)
                                  .upToFirstOccurrenceOf("\n", false, false);
    CHECK_FALSE(path.containsChar(' '));
    CHECK(path.contains("2026-11-10%2021-04-09%20Song"));
    CHECK(juce::URL::removeEscapeChars(path) == take.getFullPathName());
    CHECK(record.contains("DeletionDate=2026-11-10T21:04:09\n"));

    // A second of the same name is numbered, not overwritten.
    const juce::File song = written(scratch.get().getChildFile("Songs/Aria.musicxml"), "one");
    REQUIRE(library::moveToTrashIn(song, trash, when));
    const juce::File again = written(scratch.get().getChildFile("Songs/Aria.musicxml"), "two");
    REQUIRE(library::moveToTrashIn(again, trash, when));
    CHECK(trash.getChildFile("files/Aria.musicxml").loadFileAsString() == "one");
    CHECK(trash.getChildFile("files/Aria.2.musicxml").loadFileAsString() == "two");
    CHECK(trash.getChildFile("info/Aria.2.musicxml.trashinfo").existsAsFile());

    // Nothing to move is not moved.
    CHECK_FALSE(library::moveToTrashIn(scratch.get().getChildFile("missing"), trash, when));
}
