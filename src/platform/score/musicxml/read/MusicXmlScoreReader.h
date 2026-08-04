#pragma once

#include "MusicXmlReadContext.h"
#include "platform/score/Score.h"
#include "platform/score/musicxml/XmlDocumentAdapter.h"

// Reading a whole <score-partwise> document into a Score.
//
// The document's own shape: metadata at the head, a <part-list> declaring the
// parts, then one <part> per declared part holding its measures. Each stage is
// separate below because each fails differently -- missing metadata is nothing,
// a missing part list is recoverable, and no parts at all is a failure.
namespace score::musicxml
{
// Work title, movement title, composer, lyricist, and the encoding software
// string. None of it is required, and none of its absence is worth a
// diagnostic: plenty of real scores carry no metadata at all.
void readMetadata(const XmlNode& root, Score& score);

// The <part-list>. Fills in `score.parts` with empty parts, applies invariant 6
// to their identifiers, and returns the map from each part's *source* id to its
// index, which is what the <part> elements below reference.
[[nodiscard]] std::map<std::string, std::size_t>
readPartList(const XmlNode& root, ReadContext& context, Score& score);

// The <part> elements, each into the part the part list declared for it.
void readParts(
    const XmlNode& root,
    ReadContext& context,
    Score& score,
    std::map<std::string, std::size_t>& partIndexById);

// Walk the whole document counting elements the importer has never heard of, so
// they can be summarised by name rather than dropped in silence (task 7.2).
void countUnrecognisedElements(const XmlNode& node, ReadContext& context);
} // namespace score::musicxml
