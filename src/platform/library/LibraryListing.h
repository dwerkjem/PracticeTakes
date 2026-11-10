#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "LibraryLayout.h"
#include "TakeManifest.h"

// What the Library window lists: every song in `Songs/` and the samples folder,
// and every take in `Takes/` (openspec/changes/media-library, decision 6).
//
// Both read the disk and listing songs imports each one, so both run on a
// worker thread. JUCE core only; tested against a temporary folder.
namespace library
{
struct SongEntry
{
    juce::File file;
    std::string title;
    std::string composer;
    std::vector<std::string> parts;

    // When it came into the library: the file's creation time, in ms since
    // 1970 UTC.
    std::int64_t addedMs = 0;

    // The shipped sample: always listed, never removable.
    bool builtIn = false;

    // False for a file that does not import; it is still listed, by name.
    bool readable = true;
};

// The index cache's name, in `Songs/`. Disposable: deleting it costs a
// rescan.
inline constexpr const char* songIndexFileName = ".library-index.json";
inline constexpr int songIndexVersion = 1;

// Every song, sorted by title: the built-in ones first, then the library's.
// Each is read from the index cache when its name, size and time still match,
// and imported otherwise; the cache is rewritten when anything changed.
[[nodiscard]] std::vector<SongEntry>
listSongs(const Layout& layout, const juce::File& samplesFolder);

struct TakeEntry
{
    juce::File folder;

    // Absent when the manifest does not read; `error` says why.
    std::optional<TakeManifest> manifest;
    std::string error;

    // The folder's size on disk, in bytes.
    std::int64_t bytes = 0;

    [[nodiscard]] std::int64_t recordedMs() const
    {
        return manifest.has_value() ? manifest->recordedMs
                                    : folder.getLastModificationTime().toMilliseconds();
    }
};

// Every take folder under `Takes/`, newest first. A folder without a whole
// manifest -- a save cut short -- is not a take and is not listed.
[[nodiscard]] std::vector<TakeEntry> listTakes(const Layout& layout);
} // namespace library
