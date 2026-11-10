#pragma once

#include <juce_core/juce_core.h>

// Where the media library keeps songs and takes on disk, and the rules for
// putting a file there (openspec/changes/media-library, decisions 1-3).
//
// JUCE core only, and every function takes its folder as an argument, so each
// rule is tested against a temporary directory and nothing here knows where the
// user's own library is except `defaultFolder()`.
namespace library
{
// `Practice Takes` under the user's Music folder. Not created by this call.
[[nodiscard]] juce::File defaultFolder();

// The library at `root`: its two folders, neither created until something goes
// in it.
struct Layout
{
    juce::File root;

    [[nodiscard]] juce::File songs() const
    {
        return root.getChildFile("Songs");
    }

    [[nodiscard]] juce::File takes() const
    {
        return root.getChildFile("Takes");
    }

    // Whether `file` is anywhere under the library folder.
    [[nodiscard]] bool contains(const juce::File& file) const
    {
        return root != juce::File() && file.isAChildOf(root);
    }
};

// The folder name a take recorded at `when` against `scoreTitle` is saved
// under: `2026-11-10 21-04-09 Title`, legal on every platform. The title is cut
// to keep the name short, and a take with no title is named by its time alone.
[[nodiscard]] juce::String takeFolderName(const juce::Time& when, const juce::String& scoreTitle);

// A folder for a new take under `takesFolder`, named by `takeFolderName`, and
// numbered ` (2)`, ` (3)` ... if one by that name exists. Not created.
[[nodiscard]] juce::File newTakeFolder(
    const juce::File& takesFolder,
    const juce::Time& when,
    const juce::String& scoreTitle);

// The file in `songsFolder` that opening `source` should use: one already
// there with exactly its bytes, or else a free name for a copy -- its own, or
// numbered ` (2)` ... before the extension. Nothing is written.
[[nodiscard]] juce::File songCopyTarget(const juce::File& songsFolder, const juce::File& source);

// What opening `source` should open: `source` itself when it is already in the
// library, otherwise its copy in `Songs/`, made now if it is not there yet.
// A default `juce::File` when the copy cannot be made; the caller says so and
// opens `source` instead, because failing to copy is no reason not to open.
[[nodiscard]] juce::File intoLibrary(const Layout& layout, const juce::File& source);

// Move `item`, a file or a folder, into the freedesktop.org trash at
// `trashHome` (its `files/` and `info/` folders, made if missing), with the
// `.trashinfo` a desktop's bin reads to put it back where it was. True when
// it is in the bin.
[[nodiscard]] bool
moveToTrashIn(const juce::File& item, const juce::File& trashHome, const juce::Time& when);

// Move `item` to the system's bin, never erasing it: a take is a recording
// that cannot be made again. On Linux, the user's freedesktop trash
// (`$XDG_DATA_HOME/Trash`, else `~/.local/share/Trash`), made if a fresh
// account has none yet, which juce::File::moveToTrash will not do; elsewhere,
// the system's own.
[[nodiscard]] bool moveToBin(const juce::File& item);
} // namespace library
