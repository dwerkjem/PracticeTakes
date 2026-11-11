#pragma once

#include <juce_core/juce_core.h>

#include <memory>
#include <optional>
#include <string>

#include "../score/musicxml/MusicXmlImportResult.h"
#include "../take/Take.h"
#include "TakeManifest.h"

// A take's folder in the library: its manifest, its microphone audio as FLAC,
// and a copy of the score it was played against (openspec/changes/media-library,
// decision 3). Writing and reading both block on the disk, and reading imports
// a score, so both run on a worker thread -- TakeSaver's and TakeLoader's --
// never the message thread, and never the audio thread.
namespace library
{
inline constexpr const char* manifestFileName = "take.json";
inline constexpr const char* microphoneFileName = "microphone.flac";

// The microphone is kept at 24 bits: lossless for anything a microphone and its
// converter deliver, at half the size of the float the take holds.
inline constexpr int microphoneBits = 24;

// Write `recorded` into `folder`, creating it, with a copy of `scoreSource` --
// the file its score was imported from -- beside it. Empty on success; on
// failure, why, as a catalogue identifier, and whatever was half-written is
// removed.
//
// The manifest is written last, under a temporary name and then renamed, so a
// folder with a `take.json` in it is a whole take, and a save cut short by a
// crash leaves a folder the listing ignores.
[[nodiscard]] std::string writeTake(
    const juce::File& folder,
    const take::Take& recorded,
    const juce::File& scoreSource,
    std::int64_t recordedMs);

// A take's manifest alone, for listing: no audio decoded, no score imported.
[[nodiscard]] DecodedManifest readManifest(const juce::File& folder);

struct LoadedTake
{
    // Null when it could not be read; `error` says why.
    std::shared_ptr<take::Take> take;
    std::string error;

    // The import of the score copy, whose score `take` holds. The shell
    // publishes it as the current score before reviewing the take.
    score::musicxml::MusicXmlImportResult scoreImport;

    // The score copy itself.
    juce::File scoreFile;
};

// Read the take in `folder` whole: manifest, audio and score, with its clock
// table rebuilt from the score at the take's scale, sample rate and loop. The
// take's `libraryFolder` is `folder`, so it is not saved again.
[[nodiscard]] LoadedTake readTake(const juce::File& folder);
} // namespace library
