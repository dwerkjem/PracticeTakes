#include "MusicXmlImporter.h"

#include <map>
#include <utility>

#include "XmlDocumentAdapter.h"
#include "container/MusicXmlContainer.h"
#include "platform/score/ScoreBuilder.h"
#include "read/MusicXmlReadContext.h"
#include "read/MusicXmlScoreReader.h"

namespace score::musicxml
{
namespace
{
MusicXmlImportResult failure(MusicXmlImportStatus status, std::string error)
{
    MusicXmlImportResult result;
    result.status = status;
    result.error = std::move(error);

    return result;
}

// Decide what kind of document this is, before trying to read a score out of
// it. Returns a failed result when it is not one this importer accepts.
std::optional<MusicXmlImportResult> rejectUnacceptableRoot(const XmlNode& root)
{
    // score-timewise nests parts inside measures instead of the other way
    // round. Converting one to the other is a whole XSLT step and its own test
    // surface, and no mainstream program exports it. Rejected explicitly rather
    // than misread.
    if (root.name == "score-timewise")
    {
        return failure(
            MusicXmlImportStatus::unsupportedDocumentType,
            "This is a timewise MusicXML document. Only partwise documents, which is "
            "what every mainstream notation program exports, can be imported.");
    }

    if (root.name != "score-partwise")
    {
        return failure(
            MusicXmlImportStatus::notMusicXml,
            "This is well-formed XML but not a MusicXML score: its root element is <" + root.name +
                ">.");
    }

    return std::nullopt;
}

MusicXmlImportResult readScore(const XmlNode& root)
{
    ReadContext context;
    Score score;

    countUnrecognisedElements(root, context);
    readMetadata(root, score);

    std::map<std::string, std::size_t> partIndexById = readPartList(root, context, score);
    readParts(root, context, score, partIndexById);

    if (score.parts.empty())
    {
        MusicXmlImportResult result =
            failure(MusicXmlImportStatus::structurallyInvalid, "The document declares no parts.");
        result.diagnostics = context.diagnostics.release();

        return result;
    }

    // Task 7.5: a document that yields nothing. design.md leaned towards
    // failing, on the grounds that it nearly always means the parse went wrong
    // rather than that the score is empty -- and that is the rule here, but
    // stated over *events* rather than over notes. A movement of nothing but
    // rests is unusual and entirely valid, and refusing it would reject a real
    // score to catch a hypothetical one. Zero events cannot be a real score;
    // zero notes can.
    if (context.eventCount == 0)
    {
        MusicXmlImportResult result = failure(
            MusicXmlImportStatus::structurallyInvalid,
            "The document contains no notes or rests, so there is no music to import.");
        result.diagnostics = context.diagnostics.release();

        return result;
    }

    score.tempoMap = TempoMap::build(std::move(context.tempoEntries));

    // The importer's diagnostics go on the score first, because the invariant
    // pass appends its own repairs to the same list as it runs.
    score.diagnostics = context.diagnostics.release();

    MusicXmlImportResult result;
    result.score = freeze(std::move(score));

    // The frozen score's diagnostics are the importer's plus whatever the
    // invariant pass added while repairing -- together, the whole account of
    // what happened to this file.
    result.diagnostics = result.score->diagnostics;
    result.status = result.diagnostics.empty() ? MusicXmlImportStatus::imported
                                               : MusicXmlImportStatus::importedWithDiagnostics;

    return result;
}
} // namespace

MusicXmlImportResult importMusicXmlDocument(const std::string& document)
{
    if (document.size() > MusicXmlImportLimits::maximumSourceBytes)
    {
        return failure(
            MusicXmlImportStatus::tooLarge, "The document is larger than this importer accepts.");
    }

    const XmlParseResult parsed = parseXmlDocument(document);

    if (parsed.status == XmlParseStatus::empty)
    {
        return failure(MusicXmlImportStatus::notMusicXml, parsed.error);
    }

    if (parsed.status != XmlParseStatus::parsed || !parsed.root.has_value())
    {
        return failure(MusicXmlImportStatus::malformedXml, parsed.error);
    }

    if (std::optional<MusicXmlImportResult> rejected = rejectUnacceptableRoot(*parsed.root);
        rejected.has_value())
    {
        return std::move(*rejected);
    }

    return readScore(*parsed.root);
}

MusicXmlImportResult importMusicXmlFile(const juce::File& file)
{
    const MusicXmlDocumentSource source = loadDocumentSource(file);

    if (source.status != MusicXmlImportStatus::imported)
    {
        return failure(source.status, source.error);
    }

    return importMusicXmlDocument(source.document);
}
} // namespace score::musicxml
