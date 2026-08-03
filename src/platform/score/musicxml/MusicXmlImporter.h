#pragma once

#include <juce_core/juce_core.h>

#include <string>

#include "MusicXmlImportResult.h"

// The MusicXML importer's entry points.
//
// Both are plain functions on the caller's thread. They start no threads, hold
// no global state, and touch nothing outside their arguments, which is what
// makes them testable synchronously -- and what lets the application run them
// on a background thread without any coordination beyond the shared_ptr the
// result carries.
//
// Where these must run: a background thread. Reading, decompressing, and
// DOM-parsing a score is unbounded work with file I/O. It cannot run on the
// message thread without freezing the UI, and it must never be anywhere near
// the audio thread.
namespace score::musicxml
{
// Import from a path. Accepts `.musicxml`, `.xml`, and compressed `.mxl`,
// distinguished by content rather than by extension.
[[nodiscard]] MusicXmlImportResult importMusicXmlFile(const juce::File& file);

// Import from a document already in memory. This is the whole importer minus
// file reading and container resolution, and it is what the unit tests drive:
// a MusicXML fixture is a string literal, so most of the suite needs no files
// at all.
[[nodiscard]] MusicXmlImportResult importMusicXmlDocument(const std::string& document);
} // namespace score::musicxml
