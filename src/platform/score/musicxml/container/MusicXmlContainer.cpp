#include "MusicXmlContainer.h"

#include <array>
#include <cstring>
#include <memory>
#include <utility>

#include "platform/score/musicxml/XmlDocumentAdapter.h"

namespace score::musicxml
{
namespace
{
// The MusicXML container specification fixes this path exactly.
constexpr const char* containerManifestPath = "META-INF/container.xml";

MusicXmlDocumentSource failure(MusicXmlImportStatus status, std::string error)
{
    MusicXmlDocumentSource source;
    source.status = status;
    source.error = std::move(error);

    return source;
}

// Read a whole zip entry, refusing before allocating if it is too large.
//
// The size checks come first and use the entry's declared uncompressed size,
// which is what makes this bomb-resistant: a 42 KB archive declaring a 4 GB
// entry is rejected without a single byte being decompressed.
bool readEntry(
    juce::ZipFile& archive,
    int index,
    const juce::ZipFile::ZipEntry& entry,
    juce::int64 containerBytes,
    std::string& text,
    MusicXmlDocumentSource& failureOut)
{
    if (entry.uncompressedSize < 0 || static_cast<juce::uint64>(entry.uncompressedSize) >
                                          MusicXmlImportLimits::maximumUncompressedBytes)
    {
        failureOut = failure(
            MusicXmlImportStatus::tooLarge,
            "The compressed score expands to more than this importer accepts.");

        return false;
    }

    // Expansion ratio is measured against the whole container rather than
    // against this entry's compressed size, which the zip directory does not
    // expose through juce::ZipFile. That comparison is the conservative one --
    // the container is at least as large as the entry -- so a real score is
    // never rejected by it, and it still catches an archive whose declared
    // contents dwarf it.
    if (containerBytes > 0 &&
        static_cast<juce::uint64>(entry.uncompressedSize) >
            static_cast<juce::uint64>(containerBytes) * MusicXmlImportLimits::maximumExpansionRatio)
    {
        failureOut = failure(
            MusicXmlImportStatus::tooLarge,
            "The compressed score expands far more than a real score does, so it "
            "was refused rather than unpacked.");

        return false;
    }

    const std::unique_ptr<juce::InputStream> stream(archive.createStreamForEntry(index));

    if (stream == nullptr)
    {
        failureOut = failure(
            MusicXmlImportStatus::invalidContainer,
            "An entry inside the compressed score could not be opened.");

        return false;
    }

    juce::MemoryOutputStream buffer;

    // Copy a bounded amount rather than to the end of the stream. The declared
    // size was checked above; this stops a stream that keeps producing bytes
    // past what it declared, which is the other half of the same attack.
    buffer.writeFromInputStream(
        *stream, static_cast<juce::int64>(MusicXmlImportLimits::maximumUncompressedBytes) + 1);

    if (buffer.getDataSize() > MusicXmlImportLimits::maximumUncompressedBytes)
    {
        failureOut = failure(
            MusicXmlImportStatus::tooLarge,
            "The compressed score expands to more than this importer accepts.");

        return false;
    }

    text.assign(static_cast<const char*>(buffer.getData()), buffer.getDataSize());

    return true;
}
} // namespace

bool looksLikeZipContainer(const void* bytes, std::size_t size) noexcept
{
    // "PK\x03\x04" -- the local file header every non-empty ZIP starts with.
    constexpr std::array<unsigned char, 4> signature{0x50, 0x4B, 0x03, 0x04};

    if (bytes == nullptr || size < signature.size())
    {
        return false;
    }

    return std::memcmp(bytes, signature.data(), signature.size()) == 0;
}

MusicXmlDocumentSource resolveContainer(const juce::File& file)
{
    juce::ZipFile archive(file);

    if (archive.getNumEntries() <= 0)
    {
        return failure(
            MusicXmlImportStatus::invalidContainer, "The compressed score contains no entries.");
    }

    const juce::int64 containerBytes = file.getSize();

    const juce::ZipFile::ZipEntry* manifestEntry =
        archive.getEntry(juce::String(containerManifestPath), true);

    if (manifestEntry == nullptr)
    {
        return failure(
            MusicXmlImportStatus::invalidContainer,
            "The compressed score has no META-INF/container.xml, so there is no way to "
            "tell which entry inside it is the score.");
    }

    // getEntry returns the entry, but createStreamForEntry wants its index.
    int manifestIndex = -1;

    for (int index = 0; index < archive.getNumEntries(); ++index)
    {
        if (archive.getEntry(index) == manifestEntry)
        {
            manifestIndex = index;
            break;
        }
    }

    if (manifestIndex < 0)
    {
        return failure(
            MusicXmlImportStatus::invalidContainer,
            "The compressed score's manifest could not be located inside it.");
    }

    std::string manifestText;
    MusicXmlDocumentSource readFailure;

    if (!readEntry(
            archive, manifestIndex, *manifestEntry, containerBytes, manifestText, readFailure))
    {
        return readFailure;
    }

    const XmlParseResult manifest = parseXmlDocument(manifestText);

    if (manifest.status != XmlParseStatus::parsed || !manifest.root.has_value())
    {
        return failure(
            MusicXmlImportStatus::invalidContainer,
            "The compressed score's META-INF/container.xml is not readable XML.");
    }

    // <container><rootfiles><rootfile full-path="score.xml"/></rootfiles></container>
    const XmlNode* rootfiles = findChild(*manifest.root, "rootfiles");

    if (rootfiles == nullptr)
    {
        return failure(
            MusicXmlImportStatus::invalidContainer,
            "The compressed score's manifest lists no root files.");
    }

    // The first <rootfile> is the score; later ones are alternate
    // representations the specification allows and this importer does not read.
    const XmlNode* rootfile = findChild(*rootfiles, "rootfile");
    const std::string fullPath =
        rootfile != nullptr ? attributeValue(*rootfile, "full-path") : std::string{};

    if (fullPath.empty())
    {
        return failure(
            MusicXmlImportStatus::invalidContainer,
            "The compressed score's manifest does not name a root score document.");
    }

    const juce::ZipFile::ZipEntry* rootEntry =
        archive.getEntry(juce::String(juce::CharPointer_UTF8(fullPath.c_str())), true);
    int rootIndex = -1;

    for (int index = 0; index < archive.getNumEntries(); ++index)
    {
        if (archive.getEntry(index) == rootEntry)
        {
            rootIndex = index;
            break;
        }
    }

    if (rootEntry == nullptr || rootIndex < 0)
    {
        return failure(
            MusicXmlImportStatus::invalidContainer, "The compressed score's manifest names \"" +
                                                        fullPath + "\", which is not in the file.");
    }

    std::string documentText;

    if (!readEntry(archive, rootIndex, *rootEntry, containerBytes, documentText, readFailure))
    {
        return readFailure;
    }

    MusicXmlDocumentSource source;
    source.status = MusicXmlImportStatus::imported;
    source.document = std::move(documentText);
    source.rootEntryName = fullPath;

    return source;
}

MusicXmlDocumentSource loadDocumentSource(const juce::File& file)
{
    if (!file.existsAsFile())
    {
        return failure(
            MusicXmlImportStatus::notFound,
            "There is no file at \"" + file.getFullPathName().toStdString() + "\".");
    }

    const juce::int64 size = file.getSize();

    if (size < 0)
    {
        return failure(MusicXmlImportStatus::unreadable, "The file could not be read.");
    }

    if (static_cast<juce::uint64>(size) > MusicXmlImportLimits::maximumSourceBytes)
    {
        return failure(
            MusicXmlImportStatus::tooLarge, "The file is larger than this importer accepts.");
    }

    juce::FileInputStream stream(file);

    if (!stream.openedOk())
    {
        return failure(MusicXmlImportStatus::unreadable, "The file could not be opened.");
    }

    juce::MemoryOutputStream buffer;
    buffer.writeFromInputStream(stream, size);

    const std::string bytes(static_cast<const char*>(buffer.getData()), buffer.getDataSize());

    if (looksLikeZipContainer(bytes.data(), bytes.size()))
    {
        return resolveContainer(file);
    }

    if (bytes.empty())
    {
        return failure(MusicXmlImportStatus::notMusicXml, "The file is empty.");
    }

    MusicXmlDocumentSource source;
    source.status = MusicXmlImportStatus::imported;
    source.document = bytes;

    return source;
}
} // namespace score::musicxml
