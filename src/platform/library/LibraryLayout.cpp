#include "LibraryLayout.h"

namespace
{
// How much of a score's title a take's folder name keeps.
constexpr int titleCharacters = 60;

// The first name in `folder` from `stem` and `extension` that nothing has
// taken: `stem`, then `stem (2)`, `stem (3)` ...
juce::File
freeName(const juce::File& folder, const juce::String& stem, const juce::String& extension)
{
    juce::File candidate = folder.getChildFile(stem + extension);
    for (int number = 2; candidate.exists(); ++number)
    {
        candidate = folder.getChildFile(stem + " (" + juce::String(number) + ")" + extension);
    }
    return candidate;
}

bool sameBytes(const juce::File& one, const juce::File& other)
{
    if (one.getSize() != other.getSize())
    {
        return false;
    }
    juce::MemoryBlock first;
    juce::MemoryBlock second;
    return one.loadFileAsData(first) && other.loadFileAsData(second) && first == second;
}
} // namespace

namespace library
{
juce::File defaultFolder()
{
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory)
        .getChildFile("Practice Takes");
}

juce::String takeFolderName(const juce::Time& when, const juce::String& scoreTitle)
{
    const juce::String stamp = when.formatted("%Y-%m-%d %H-%M-%S");
    const juce::String title = scoreTitle.trim().substring(0, titleCharacters).trim();
    return juce::File::createLegalFileName(title.isEmpty() ? stamp : stamp + " " + title);
}

juce::File
newTakeFolder(const juce::File& takesFolder, const juce::Time& when, const juce::String& scoreTitle)
{
    return freeName(takesFolder, takeFolderName(when, scoreTitle), {});
}

juce::File songCopyTarget(const juce::File& songsFolder, const juce::File& source)
{
    // The same score saved twice is still one song, whatever it is called.
    if (songsFolder.isDirectory())
    {
        for (const auto& entry : juce::RangedDirectoryIterator(
                 songsFolder, false, "*" + source.getFileExtension(), juce::File::findFiles))
        {
            if (sameBytes(entry.getFile(), source))
            {
                return entry.getFile();
            }
        }
    }
    return freeName(songsFolder, source.getFileNameWithoutExtension(), source.getFileExtension());
}

juce::File intoLibrary(const Layout& layout, const juce::File& source)
{
    if (layout.contains(source))
    {
        return source;
    }
    const juce::File songs = layout.songs();
    if (!songs.createDirectory())
    {
        return {};
    }
    juce::File target = songCopyTarget(songs, source);
    if (target.existsAsFile())
    {
        return target;
    }
    return source.copyFileTo(target) ? target : juce::File();
}

bool moveToTrashIn(const juce::File& item, const juce::File& trashHome, const juce::Time& when)
{
    if (!item.exists())
    {
        return false;
    }
    const juce::File files = trashHome.getChildFile("files");
    const juce::File info = trashHome.getChildFile("info");
    if (!files.createDirectory() || !info.createDirectory())
    {
        return false;
    }

    // A name free in both folders, as the specification asks.
    const juce::String stem = item.getFileNameWithoutExtension();
    const juce::String extension = item.isDirectory() ? juce::String() : item.getFileExtension();
    juce::String name = stem + extension;
    for (int number = 2;
         files.getChildFile(name).exists() || info.getChildFile(name + ".trashinfo").exists();
         ++number)
    {
        name = stem + "." + juce::String(number) + extension;
    }

    const juce::File record = info.getChildFile(name + ".trashinfo");
    const juce::String path =
        juce::URL::addEscapeChars(item.getFullPathName(), false, false).replace("%2F", "/");
    if (!record.replaceWithText(
            "[Trash Info]\nPath=" + path + "\nDeletionDate=" + when.formatted("%Y-%m-%dT%H:%M:%S") +
                "\n",
            false, false, "\n"))
    {
        return false;
    }

    const juce::File destination = files.getChildFile(name);
    bool moved = item.moveFileTo(destination);
    // A folder on another drive than the bin cannot be renamed into it:
    // copied, and only then removed.
    if (!moved && item.isDirectory() && item.copyDirectoryTo(destination))
    {
        moved = item.deleteRecursively();
    }
    if (!moved)
    {
        static_cast<void>(record.deleteFile());
    }
    return moved;
}

bool moveToBin(const juce::File& item)
{
#if JUCE_LINUX || JUCE_BSD
    const juce::String dataHome = juce::SystemStats::getEnvironmentVariable("XDG_DATA_HOME", {});
    const juce::File trash =
        juce::File::isAbsolutePath(dataHome)
            ? juce::File(dataHome).getChildFile("Trash")
            : juce::File("~/.local/share/Trash");
    return moveToTrashIn(item, trash, juce::Time::getCurrentTime());
#else
    return item.moveToTrash();
#endif
}
} // namespace library
