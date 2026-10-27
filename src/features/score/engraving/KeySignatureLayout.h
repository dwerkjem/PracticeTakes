#pragma once

#include <vector>

#include "Accidental.h"
#include "StaffGeometry.h"
#include "platform/score/Pitch.h"
#include "platform/score/ScoreAttributes.h"

// Where a key signature's accidentals go (no JUCE dependency).
namespace engraving
{
// One accidental of a key signature, in the order it is drawn.
struct KeySignatureAccidental
{
    score::Step step = score::Step::f;

    StaffSteps position = 0;

    AccidentalKind kind = AccidentalKind::sharp;
};

// The accidentals of `key` as drawn in `clef`, left to right.
//
// Empty for a key of no sharps or flats: C major draws nothing, and a renderer
// that drew seven naturals instead would be wrong rather than thorough.
//
// Where each accidental sits is **convention, not arithmetic** -- the octave
// each one is written in is chosen to keep the group on the staff, and no rule
// that holds for one clef holds for all of them. So this is one hand-checked
// table for a G clef on line 2, offset by one half space for a C clef on line 3
// and two for an F clef on line 4; those three reproduce the engraved positions
// for treble, alto and bass exactly, for all seven sharps and all seven flats.
// See design.md decision 3 for the rule that was tried instead and where it
// disagreed.
//
// A clef of a known sign on an unusual line keeps the pitches and shifts by the
// line difference, which may leave the group high or low on the staff;
// `keySignatureIsConventional` is how a caller knows to say so. An unpitched
// clef draws no key signature.
[[nodiscard]] std::vector<KeySignatureAccidental>
keySignatureAccidentals(const score::Clef& clef, const score::KeySignature& key);

// Whether this clef is one of the three whose key signature positions were
// checked against an engraved reference, rather than derived by shifting.
[[nodiscard]] bool keySignatureIsConventional(const score::Clef& clef);

// How many accidentals the signature holds, clamped to the seven the circle of
// fifths provides. A file declaring more than seven is malformed; it is clamped
// rather than refused, because the notes are still worth drawing.
[[nodiscard]] int keySignatureAccidentalCount(const score::KeySignature& key);
} // namespace engraving
