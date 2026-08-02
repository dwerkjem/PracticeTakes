#pragma once

#include <cstddef>
#include <vector>

#include "MusicXmlReadContext.h"
#include "platform/score/Measure.h"
#include "platform/score/MusicalTime.h"
#include "platform/score/Score.h"
#include "platform/score/musicxml/XmlDocumentAdapter.h"

// Reading one <measure> into the model.
//
// This is where the format is at its least forgiving, and it is deliberately
// the smallest file that can hold the whole of it: a measure's meaning is
// positional, and splitting the cursor from the elements that move it would
// make it harder to see, not easier.
//
// The cursor is the thing to understand. MusicXML writes a measure as a stream
// of elements sharing **one write position**. <note> advances it, <backup>
// moves it back, <forward> skips it ahead. A file writes voice 1 through the
// bar, backs the cursor up to the start, then writes voice 2 over the same
// span -- so events are not in time order in the file, and a mishandled backup
// shifts an entire voice by a fraction of a bar without anything crashing.
namespace score::musicxml
{
// Read `measureNode` and append the finished measure to `part`.
//
// `state` carries divisions and time signature in and out, because both can
// change partway through a part. `pendingVoices` receives the measure's events
// with their tie flags intact, for the tie resolver to link once the whole part
// has been read.
void readMeasure(
    const XmlNode& measureNode,
    ReadContext& context,
    Part& part,
    PartCursorState& state,
    std::size_t measureIndex,
    Tick measureStart,
    std::vector<PendingVoice>& pendingVoices);
} // namespace score::musicxml
