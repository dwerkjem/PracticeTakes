#pragma once

#include <map>
#include <utility>

#include "Accidental.h"
#include "platform/score/Pitch.h"
#include "platform/score/ScoreAttributes.h"
#include "platform/score/ScoreEvent.h"

// Which notes need an accidental drawn on them (no JUCE dependency).
//
// An accidental is not a property of a note, it is a property of a note *in
// context*: it is drawn when the note's alteration differs from the one already
// in force for that step in that octave, which the key signature sets at the
// start of each bar and any accidental already drawn in the bar then overrides.
//
// This is in its own file with its own tests because the failure mode is subtle
// and cumulative rather than loud. A missing natural or a repeated sharp does
// not crash anything; it is a wrong note that a player plays.
namespace engraving
{
// The alterations in force within one measure, for one staff.
//
// Reset at every barline by constructing a new one -- which is the rule, stated
// as a lifetime rather than as a method somebody has to remember to call.
class MeasureAccidentals
{
  public:
    // Seeded from the key signature: two sharps means every F and every C is
    // sharp until something says otherwise, in every octave.
    explicit MeasureAccidentals(const score::KeySignature& key);

    // The accidental to draw on this note, and record it as now in force.
    //
    // `isTieContinuation` suppresses the sign entirely: the accidental belongs
    // to the note that was struck, not to the one holding it on. That is the
    // common case of a tie across a barline, where repeating the sign would be
    // wrong -- and it does not disturb what is in force, because the pitch has
    // not changed.
    [[nodiscard]] AccidentalKind accidentalFor(const score::Pitch& pitch, bool isTieContinuation);

    // What this bar currently reads for a step and octave. Exposed for tests
    // and for a caller that wants to ask without recording an answer.
    [[nodiscard]] int alterationInForce(score::Step step, int octave) const;

  private:
    // Keyed by octave and step together: a C-sharp in one octave says nothing
    // about the C two octaves up, which is the rule modern notation follows and
    // the one MusicXML assumes.
    using StepInOctave = std::pair<int, score::Step>;

    // Alterations the key signature provides, by step. Checked when the bar has
    // said nothing about that step and octave yet.
    std::map<score::Step, int> fromKey;

    std::map<StepInOctave, int> drawnInThisMeasure;
};

// The alteration a key signature gives a step: +1 for F in a key of one sharp
// or more, -1 for B in a key of one flat or more, 0 otherwise.
[[nodiscard]] int keyAlterationFor(const score::KeySignature& key, score::Step step);
} // namespace engraving
