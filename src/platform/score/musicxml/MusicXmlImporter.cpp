#include "MusicXmlImporter.h"
#include "../../../application/localisation/LocalisationRules.h"

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
MusicXmlImportResult
failure(MusicXmlImportStatus status, std::string_view error, l10n::NamedArguments arguments = {})
{
    MusicXmlImportResult result;
    result.status = status;
    result.error = std::string(error);
    result.errorArguments = std::move(arguments);

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
            l10n::key("score.import.error.timewise"));
    }

    if (root.name != "score-partwise")
    {
        return failure(
            MusicXmlImportStatus::notMusicXml, l10n::key("score.import.error.not-musicxml"),
            {{"element", root.name}});
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
        MusicXmlImportResult result = failure(
            MusicXmlImportStatus::structurallyInvalid, l10n::key("score.import.error.no-parts"));
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
            MusicXmlImportStatus::structurallyInvalid, l10n::key("score.import.error.no-music"));
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
            MusicXmlImportStatus::tooLarge, l10n::key("score.import.error.document-too-large"));
    }

    const XmlParseResult parsed = parseXmlDocument(document);

    if (parsed.status == XmlParseStatus::empty)
    {
        return failure(MusicXmlImportStatus::notMusicXml, parsed.error, parsed.errorArguments);
    }

    if (parsed.status != XmlParseStatus::parsed || !parsed.root.has_value())
    {
        return failure(MusicXmlImportStatus::malformedXml, parsed.error, parsed.errorArguments);
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
        return failure(source.status, source.error, source.errorArguments);
    }

    return importMusicXmlDocument(source.document);
}
} // namespace score::musicxml
