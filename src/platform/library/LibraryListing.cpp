#include "LibraryListing.h"

#include <algorithm>
#include <map>
#include <utility>

#include "../score/musicxml/MusicXmlImporter.h"
#include "TakeFiles.h"

namespace
{
using library::SongEntry;

// What decides whether a cached entry is still the file's.
struct Key
{
    std::int64_t size = 0;
    std::int64_t modifiedMs = 0;

    bool operator==(const Key&) const = default;
};

Key keyOf(const juce::File& file)
{
    return {file.getSize(), file.getLastModificationTime().toMilliseconds()};
}

struct Cached
{
    Key key;
    SongEntry entry;
};

std::map<juce::String, Cached> readIndex(const juce::File& file)
{
    std::map<juce::String, Cached> index;
    const juce::var parsed = juce::JSON::parse(file);
    if (static_cast<int>(parsed["version"]) != library::songIndexVersion)
    {
        return index;
    }
    if (const auto* songs = parsed["songs"].getArray(); songs != nullptr)
    {
        for (const juce::var& song : *songs)
        {
            Cached cached;
            cached.key = {
                static_cast<juce::int64>(song["size"]), static_cast<juce::int64>(song["modified"])};
            cached.entry.title = song["title"].toString().toStdString();
            cached.entry.composer = song["composer"].toString().toStdString();
            cached.entry.readable = static_cast<bool>(song["readable"]);
            if (const auto* parts = song["parts"].getArray(); parts != nullptr)
            {
                for (const juce::var& part : *parts)
                {
                    cached.entry.parts.push_back(part.toString().toStdString());
                }
            }
            index[song["name"].toString()] = std::move(cached);
        }
    }
    return index;
}

void writeIndex(const juce::File& file, const std::map<juce::String, Cached>& index)
{
    juce::Array<juce::var> songs;
    for (const auto& [name, cached] : index)
    {
        auto* song = new juce::DynamicObject();
        song->setProperty("name", name);
        song->setProperty("size", static_cast<juce::int64>(cached.key.size));
        song->setProperty("modified", static_cast<juce::int64>(cached.key.modifiedMs));
        song->setProperty("title", juce::String(cached.entry.title));
        song->setProperty("composer", juce::String(cached.entry.composer));
        song->setProperty("readable", cached.entry.readable);
        juce::Array<juce::var> parts;
        for (const auto& part : cached.entry.parts)
        {
            parts.add(juce::String(part));
        }
        song->setProperty("parts", parts);
        songs.add(juce::var(song));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty("version", library::songIndexVersion);
    root->setProperty("songs", songs);
    // A cache: failing to write it costs a rescan next time, nothing more.
    static_cast<void>(file.replaceWithText(juce::JSON::toString(juce::var(root))));
}

SongEntry read(const juce::File& file)
{
    SongEntry entry;
    const auto imported = score::musicxml::importMusicXmlFile(file);
    if (imported.score == nullptr)
    {
        entry.readable = false;
        entry.title = file.getFileNameWithoutExtension().toStdString();
        return entry;
    }
    const auto& metadata = imported.score->metadata;
    entry.title = !metadata.workTitle.empty() ? metadata.workTitle
                  : !metadata.movementTitle.empty()
                      ? metadata.movementTitle
                      : file.getFileNameWithoutExtension().toStdString();
    entry.composer = metadata.composer;
    for (const auto& part : imported.score->parts)
    {
        entry.parts.push_back(part.name);
    }
    return entry;
}

bool isScore(const juce::File& file)
{
    return file.hasFileExtension("musicxml;xml;mxl") && !file.getFileName().startsWithChar('.');
}

// The songs in `folder`, each from the cache in `index` when it still
// matches, read otherwise. Sets `changed` when the index needs writing.
std::vector<SongEntry> songsIn(
    const juce::File& folder,
    std::map<juce::String, Cached>& index,
    bool builtIn,
    bool& changed)
{
    std::vector<SongEntry> songs;
    if (!folder.isDirectory())
    {
        return songs;
    }
    std::map<juce::String, Cached> seen;
    for (const auto& item :
         juce::RangedDirectoryIterator(folder, false, "*", juce::File::findFiles))
    {
        const juce::File file = item.getFile();
        if (!isScore(file))
        {
            continue;
        }
        const Key key = keyOf(file);
        Cached cached;
        if (const auto found = index.find(file.getFileName());
            found != index.end() && found->second.key == key)
        {
            cached = found->second;
        }
        else
        {
            cached = {key, read(file)};
            changed = true;
        }
        cached.entry.file = file;
        cached.entry.builtIn = builtIn;
        cached.entry.addedMs = file.getCreationTime().toMilliseconds();
        songs.push_back(cached.entry);
        seen[file.getFileName()] = std::move(cached);
    }
    changed = changed || seen.size() != index.size();
    index = std::move(seen);
    return songs;
}

bool byTitle(const SongEntry& one, const SongEntry& other)
{
    const int order = juce::String(one.title).compareNatural(juce::String(other.title));
    return order != 0 ? order < 0 : one.file.getFileName() < other.file.getFileName();
}
} // namespace

namespace library
{
std::vector<SongEntry> listSongs(const Layout& layout, const juce::File& samplesFolder)
{
    // The samples are few and this build wrote them: read each, no cache.
    std::map<juce::String, Cached> noIndex;
    bool ignored = false;
    std::vector<SongEntry> songs = songsIn(samplesFolder, noIndex, true, ignored);
    std::sort(songs.begin(), songs.end(), byTitle);

    const juce::File indexFile = layout.songs().getChildFile(songIndexFileName);
    std::map<juce::String, Cached> index = readIndex(indexFile);
    bool changed = false;
    std::vector<SongEntry> own = songsIn(layout.songs(), index, false, changed);
    if (changed && layout.songs().isDirectory())
    {
        writeIndex(indexFile, index);
    }
    std::sort(own.begin(), own.end(), byTitle);
    songs.insert(songs.end(), own.begin(), own.end());
    return songs;
}

std::vector<TakeEntry> listTakes(const Layout& layout)
{
    std::vector<TakeEntry> takes;
    const juce::File folder = layout.takes();
    if (!folder.isDirectory())
    {
        return takes;
    }
    for (const auto& item :
         juce::RangedDirectoryIterator(folder, false, "*", juce::File::findDirectories))
    {
        const juce::File takeFolder = item.getFile();
        if (!takeFolder.getChildFile(manifestFileName).existsAsFile())
        {
            continue;
        }
        TakeEntry entry;
        entry.folder = takeFolder;
        DecodedManifest decoded = readManifest(takeFolder);
        entry.manifest = std::move(decoded.manifest);
        entry.error = std::move(decoded.error);
        for (const auto& file :
             juce::RangedDirectoryIterator(takeFolder, false, "*", juce::File::findFiles))
        {
            entry.bytes += file.getFileSize();
        }
        takes.push_back(std::move(entry));
    }
    std::sort(
        takes.begin(), takes.end(), [](const TakeEntry& one, const TakeEntry& other)
        { return one.recordedMs() > other.recordedMs(); });
    return takes;
}
} // namespace library
