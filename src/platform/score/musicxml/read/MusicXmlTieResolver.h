#pragma once

#include <vector>

#include "MusicXmlReadContext.h"
#include "platform/score/Score.h"

// Linking a part's ties, once all of its measures have been read.
//
// A tie is a link between two events, and an EventRef naming an event is only
// meaningful once that event's position within its voice is final -- so this
// cannot happen while a measure is being read. It also cannot happen after the
// invariant pass, which sorts events and would leave any ref built beforehand
// pointing at the wrong note. Between the two is the only correct moment, and
// this is it.
namespace score::musicxml
{
// Match each tie start in `part` to its stop and set both ends of the link.
//
// Unmatched ends are dropped with a diagnostic rather than left dangling: a
// half-linked tie is a null dereference waiting to happen in every consumer,
// and invariant 4 would remove it anyway -- doing it here just means the
// diagnostic can name the bar it happened in.
void resolveTies(
    ReadContext& context,
    Part& part,
    const std::vector<std::vector<PendingVoice>>& pendingByMeasure);
} // namespace score::musicxml
