#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "platform/score/Diagnostic.h"
#include "platform/score/Score.h"

// What an import can return, following the repository's codec convention: a
// scoped `enum class ...Status`, a `struct ...Result` carrying an optional
// payload and an error string, and a distinct status for partial success --
// the same shape as BenchmarkRecordCodec, SettingsTransferCodec, and
// WorkspaceCatalogCodec.
namespace score::musicxml
{
enum class MusicXmlImportStatus
{
    // A score, with nothing worth reporting about the file.
    imported,

    // A score, plus diagnostics: content was dropped, repaired, or not
    // recognised. This is WorkspaceCatalogCodec's `loadedWithRecovery` under a
    // name that fits. It is a success -- a file that uses one construct this
    // importer does not support is still a file worth practising with.
    importedWithDiagnostics,

    // The path does not exist.
    notFound,

    // The path exists but could not be read: permissions, a directory, a device
    // that went away mid-read.
    unreadable,

    // The source file, an entry inside a container, or the ratio between them
    // exceeded a limit. A compressed score from a stranger's sharing site is a
    // plausible ZIP bomb, and it must land here rather than in the allocator.
    tooLarge,

    // Neither XML nor a ZIP container holding XML.
    notMusicXml,

    // XML, but not well-formed.
    malformedXml,

    // A ZIP container whose META-INF/container.xml is absent, unreadable, or
    // names a root document the container does not hold. Distinct from
    // `notMusicXml` because the file really is a container -- it is just an
    // inconsistent one, and saying so is more use than "not MusicXML".
    invalidContainer,

    // Well-formed XML that is not a MusicXML document this importer accepts.
    // Chiefly `score-timewise`, which is deliberately out of scope.
    unsupportedDocumentType,

    // A `score-partwise` document whose structure the importer could not make a
    // score out of at all. Content that is merely unsupported produces
    // diagnostics and a score; this is for the cases where there is nothing to
    // return.
    structurallyInvalid
};

// True exactly when the result carries a score. There is no partial score on
// failure -- an import either produces a complete, invariant-satisfying score
// or produces none.
[[nodiscard]] constexpr bool succeeded(MusicXmlImportStatus status) noexcept
{
    return status == MusicXmlImportStatus::imported ||
           status == MusicXmlImportStatus::importedWithDiagnostics;
}

struct MusicXmlImportResult
{
    MusicXmlImportStatus status = MusicXmlImportStatus::structurallyInvalid;

    // Non-null if and only if `succeeded(status)`. Shared and const because a
    // score crosses from the importing background thread to the message thread
    // and is then read by the renderer, the score tool, and the session at
    // once; `const` is what makes that safe without a lock.
    std::shared_ptr<const Score> score;

    // Empty on success. Aimed at a user, not a log.
    std::string error;

    // Present on success and on failure. A malformed document still has
    // something to say about where it went wrong.
    std::vector<Diagnostic> diagnostics;
};

// The limits that stand between this importer and a hostile file. They are
// named constants beside the code that enforces them, following
// SettingsTransferCodec::maximumDocumentBytes.
//
// SettingsTransferCodec's own 4 MB cap is far too small here: it bounds a
// settings document, and a full orchestral score is legitimately orders of
// magnitude larger.
struct MusicXmlImportLimits
{
    // Bytes on disk. Generous for notation -- a large orchestral score is a few
    // megabytes of XML -- while still bounding the DOM parse, which holds the
    // whole document in memory by design.
    static constexpr std::size_t maximumSourceBytes = 64 * 1024 * 1024;

    // Bytes after decompressing a container entry. Higher than the on-disk cap
    // because XML compresses very well and a legitimate `.mxl` is routinely a
    // tenth of its expanded size.
    static constexpr std::size_t maximumUncompressedBytes = 256 * 1024 * 1024;

    // Decompressed size divided by compressed size, for a single entry.
    // Real MusicXML lands around 10:1 to 20:1; a ZIP bomb is 1000:1 and up.
    // This sits far enough above the first to never fire on a real score and
    // far enough below the second to stop one before it is unpacked.
    static constexpr std::size_t maximumExpansionRatio = 200;
};
} // namespace score::musicxml
