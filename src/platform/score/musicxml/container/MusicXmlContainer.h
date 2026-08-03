#pragma once

#include <juce_core/juce_core.h>

#include <string>

#include "platform/score/musicxml/MusicXmlImportResult.h"

// Turning a path into the text of one MusicXML document.
//
// Three of the accepted extensions -- `.musicxml`, `.xml`, `.mxl` -- are two
// different things. The first two are the document. The third is a ZIP
// container whose `META-INF/container.xml` manifest names which entry inside it
// is the root score, and that manifest has to actually be read: the root is
// not reliably the first entry, the largest entry, or the only `.xml` entry.
//
// libmusicxml's core ships no ZIP reader, so this is where juce::ZipFile enters
// the repository. It is the reason this file depends on JUCE while the score
// model and the parser adapter do not.
namespace score::musicxml
{
struct MusicXmlDocumentSource
{
    // On success, `imported`. Otherwise the failure, using the same status enum
    // the whole import shares so a caller never has to translate between two.
    MusicXmlImportStatus status = MusicXmlImportStatus::notMusicXml;

    // The document text. Empty unless the status is `imported`.
    std::string document;

    // For a container, the entry the manifest named. Empty for a plain
    // document. Worth carrying: when a container misbehaves, the first useful
    // question is which entry was actually opened.
    std::string rootEntryName;

    std::string error;
};

// Read `file` and return the MusicXML document text inside it.
//
// Whether a file is a container is decided by its first four bytes, not by its
// extension. Exporters do produce `.xml` files that are really containers and
// `.mxl` files that are really plain documents, and the content is the thing
// that has to be parsed either way.
//
// This performs file I/O and decompression, both unbounded; it belongs on a
// background thread, never on the message or audio thread.
[[nodiscard]] MusicXmlDocumentSource loadDocumentSource(const juce::File& file);

// The container half, exposed for tests that want to drive it directly. `file`
// must already be known to be a ZIP.
[[nodiscard]] MusicXmlDocumentSource resolveContainer(const juce::File& file);

// Whether `bytes` begins with the ZIP local-file-header signature.
[[nodiscard]] bool looksLikeZipContainer(const void* bytes, std::size_t size) noexcept;
} // namespace score::musicxml
