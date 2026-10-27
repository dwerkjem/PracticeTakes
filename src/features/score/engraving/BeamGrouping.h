#pragma once

#include <cstddef>
#include <vector>

#include "platform/score/MusicalTime.h"
#include "platform/score/ScoreAttributes.h"

// Which notes beam together (no JUCE dependency).
//
// A beam group is a metrical decision before it is a visual one. Beaming that
// ignores the time signature does not merely look unidiomatic: it tells a
// reader the wrong metre, so six eighths in 6/8 beamed in pairs read as 3/4.
// That is why the grouping lives here, as a function over onsets and a time
// signature, rather than falling out of whatever notes happened to be adjacent.
//
// **The beaming written in the file is not an input.** MusicXML carries
// `<beam>` elements per note, and the importer reads and drops them -- the
// model refuses to hold a beam group, by invariant 10, for the same reason it
// refuses to hold a stem direction. So the grouping is derived, and the layout
// says so in a notice rather than letting a reader assume the file's own
// beaming was honoured.
namespace engraving
{
// One event of one voice, in the order a reader meets it, reduced to what
// grouping needs. Rests, grace notes and anything a quarter note or longer come
// in with `beamLevels == 0`: they are not beamed, and they break a group.
struct BeamCandidate
{
    score::Tick onset = 0;
    score::Tick duration = 0;

    // How many beams this note carries: one for an eighth, two for a
    // sixteenth, and so on. Zero for a note that is never beamed.
    int beamLevels = 0;
};

// Ticks in one beam group of this time signature.
//
// Simple metres group by the beat: 3/4 beams as three groups of a quarter, 4/4
// as four. Compound metres group by the dotted beat, which is three of the
// notated beat units: 6/8 is two groups of three eighths, 9/8 three, 12/8 four.
// 3/8 is compound by this rule and so beams as one group of three, which is
// what an engraver writes.
//
// A metre this cannot read -- a non-positive beats or beat type -- returns zero,
// and a zero-length group beams nothing rather than everything.
[[nodiscard]] score::Tick beamGroupTicks(const score::TimeSignature& time);

// Whether the metre is compound: the beat divides into three rather than two,
// so the group is a dotted beat. True for 3/8, 6/8, 9/8, 12/8 and their
// sixteenth-note equivalents; false for every x/4 and x/2.
[[nodiscard]] bool isCompoundMetre(const score::TimeSignature& time);

// The runs of candidates that beam together, as indices into `candidates`.
//
// Every returned run holds at least two indices: a single eighth takes a flag,
// not a beam of its own. A run breaks at a group boundary, at anything with no
// beam levels, and at a gap in time -- two eighths either side of a rest are
// not adjacent, whatever their indices say.
//
// `candidates` is expected in onset order, which is the order one voice of one
// measure is read in. A candidate whose onset goes backwards is treated as a
// break rather than reordered: the caller's order is the reader's order.
[[nodiscard]] std::vector<std::vector<std::size_t>>
beamGroupsFor(const std::vector<BeamCandidate>& candidates, const score::TimeSignature& time);
} // namespace engraving
