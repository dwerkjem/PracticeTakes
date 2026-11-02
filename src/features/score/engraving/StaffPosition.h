#pragma once

#include <optional>

#include "StaffGeometry.h"
#include "platform/score/Pitch.h"
#include "platform/score/ScoreAttributes.h"

// Where a written pitch sits on a staff, given the clef (no JUCE dependency).
//
// This is the one computation the whole renderer rests on, and it reads the
// **spelling** rather than the sounding number. A C-sharp and a D-flat share a
// MIDI number and sit on different lines, which is exactly why the model keeps
// step/alter/octave as well as `midiNoteNumber` -- `Pitch.h` says so in those
// words, naming this issue.
namespace engraving
{
// Ledger lines a notehead needs, and which side of the staff they are on. Both
// counts are zero for a note inside the staff.
struct LedgerLines
{
    int above = 0;
    int below = 0;
};

// The diatonic position of a clef's reference pitch: a G clef names G4, an F
// clef names F3, a C clef names C4, and the sign sits on `Clef::line`.
//
// Returns nothing for a clef that names no pitch -- `percussion`, `TAB`,
// `none`. Such a staff is not drawn at all rather than having its notes placed
// on the middle line and hoped for; see design.md decision 2.
[[nodiscard]] std::optional<score::Pitch> clefReferencePitch(const score::Clef& clef);

// Whether this clef can place a pitch at all.
[[nodiscard]] bool isPitchedClef(const score::Clef& clef);

// The staff position of `pitch` read in `clef`, in half spaces below the top
// line.
//
// `clef.octaveChange` is **undone** rather than applied. A vocal tenor clef is a
// G clef with an octave change of -1, and MusicXML's <pitch> under it is the
// pitch that sounds -- an octave below where it reads. The renderer draws where
// it reads, so the octave change is taken back out before positioning. Getting
// this backwards puts a whole part an octave out without anything failing,
// which is why it has a test of its own.
//
// Returns nothing for an unpitched clef.
[[nodiscard]] std::optional<StaffSteps>
staffPositionFor(const score::Clef& clef, const score::Pitch& pitch);

// Ledger lines for a position. A position two half spaces beyond the staff is
// the first ledger line; one half space beyond is the space next to the staff
// and needs none.
[[nodiscard]] LedgerLines ledgerLinesFor(StaffSteps steps);

// Stem direction for a note on its own: up when it sits below the middle line,
// down when it is on or above it.
//
// This is the single-voice rule. With two or more voices in a measure the
// convention is the other one -- voice 1 up, voice 2 down, whatever the pitch --
// and `ScoreLayoutEngine` applies that instead. See design.md decision 6.
[[nodiscard]] bool stemPointsUpFor(StaffSteps steps);
} // namespace engraving
