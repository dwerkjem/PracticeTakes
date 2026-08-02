#include "MusicXmlScoreReader.h"

#include <string>
#include <utility>
#include <vector>

#include "MusicXmlMeasureReader.h"
#include "MusicXmlSubset.h"
#include "MusicXmlTieResolver.h"

namespace score::musicxml
{
namespace
{
void readPart(const XmlNode& partNode, ReadContext& context, Part& part)
{
    PartCursorState state;
    Tick measureStart = 0;

    const std::vector<const XmlNode*> measureNodes = findChildren(partNode, "measure");
    std::vector<std::vector<PendingVoice>> pendingByMeasure;
    pendingByMeasure.reserve(measureNodes.size());
    part.measures.reserve(measureNodes.size());

    for (std::size_t index = 0; index < measureNodes.size(); ++index)
    {
        std::vector<PendingVoice> pendingVoices;
        readMeasure(*measureNodes[index], context, part, state, index, measureStart, pendingVoices);
        measureStart += part.measures.back().nominalDuration;
        pendingByMeasure.push_back(std::move(pendingVoices));
    }

    resolveTies(context, part, pendingByMeasure);
}
} // namespace

void countUnrecognisedElements(const XmlNode& node, ReadContext& context)
{
    for (const XmlNode& child : node.children)
    {
        // An unsupported construct is reported where it is met, with a musical
        // location. Counting it here as well would say the same thing twice.
        if (unsupportedConstructs().count(child.name) > 0)
        {
            continue;
        }

        if (knownElements().count(child.name) == 0)
        {
            context.diagnostics.noteUnrecognisedElement(child.name);
        }

        countUnrecognisedElements(child, context);
    }
}

void readMetadata(const XmlNode& root, Score& score)
{
    if (const XmlNode* work = findChild(root, "work"); work != nullptr)
    {
        score.metadata.workTitle = childValue(*work, "work-title");
    }

    score.metadata.movementTitle = childValue(root, "movement-title");

    const XmlNode* identification = findChild(root, "identification");

    if (identification == nullptr)
    {
        return;
    }

    for (const XmlNode* creator : findChildren(*identification, "creator"))
    {
        const std::string type = attributeValue(*creator, "type");

        if (type == "composer" && score.metadata.composer.empty())
        {
            score.metadata.composer = creator->value;
        }
        else if ((type == "lyricist" || type == "poet") && score.metadata.lyricist.empty())
        {
            score.metadata.lyricist = creator->value;
        }
    }

    if (const XmlNode* encoding = findChild(*identification, "encoding"); encoding != nullptr)
    {
        // Worth keeping and worth surfacing: exporter dialects differ more than
        // the format suggests, so this is the first thing anyone wants to know
        // when a file misbehaves.
        score.metadata.encodingSoftware = childValue(*encoding, "software");
    }
}

std::map<std::string, std::size_t>
readPartList(const XmlNode& root, ReadContext& context, Score& score)
{
    std::map<std::string, std::size_t> partIndexById;
    const XmlNode* partList = findChild(root, "part-list");

    if (partList == nullptr)
    {
        return partIndexById;
    }

    for (const XmlNode* scorePart : findChildren(*partList, "score-part"))
    {
        Part part;
        part.id = attributeValue(*scorePart, "id");
        part.name = childValue(*scorePart, "part-name");
        part.abbreviation = childValue(*scorePart, "part-abbreviation");

        // Invariant 6 wants unique, non-empty identifiers, and the model's own
        // repair would generate one anyway. Doing it here means the <part>
        // elements can still be matched by whatever the file used -- including
        // the empty string -- which the model would no longer know.
        const std::string sourceId = part.id;
        const bool duplicate = partIndexById.count(sourceId) > 0;

        if (part.id.empty() || duplicate)
        {
            part.id = "P-generated-" + std::to_string(++context.generatedPartIds);

            DiagnosticLocation location;
            location.partId = part.id;

            context.diagnostics.addRepair(
                location, "score-part",
                duplicate ? "Two parts in the file share the identifier \"" + sourceId +
                                "\", so the second was given a new one."
                          : "A part in the file has no identifier, so one was generated for it.");
        }

        // Keyed by the *source* id, because that is what <part id="..."> says. A
        // duplicate keeps pointing at the first part that claimed it, which is
        // how a reader resolves the reference.
        if (!duplicate)
        {
            partIndexById.emplace(sourceId, score.parts.size());
        }

        score.parts.push_back(std::move(part));
    }

    return partIndexById;
}

void readParts(
    const XmlNode& root,
    ReadContext& context,
    Score& score,
    std::map<std::string, std::size_t>& partIndexById)
{
    for (const XmlNode* partNode : findChildren(root, "part"))
    {
        const auto found = partIndexById.find(attributeValue(*partNode, "id"));

        if (found != partIndexById.end())
        {
            readPart(*partNode, context, score.parts[found->second]);

            continue;
        }

        // Music for a part the <part-list> never declared. Dropping it would
        // lose a whole staff of the score, so it gets a part of its own.
        Part part;
        part.id = "P-generated-" + std::to_string(++context.generatedPartIds);

        DiagnosticLocation location;
        location.partId = part.id;
        context.diagnostics.addRepair(
            location, "part",
            "The file contains music for a part that its part list does not declare, so a part "
            "was created for it.");

        score.parts.push_back(std::move(part));
        readPart(*partNode, context, score.parts.back());
    }
}
} // namespace score::musicxml
