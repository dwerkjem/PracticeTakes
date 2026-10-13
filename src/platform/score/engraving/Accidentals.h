#pragma once

#include <optional>
#include <vector>

#include "GlyphMetrics.h"

#include "platform/score/Pitch.h"
#include "platform/score/ScoreAttributes.h"

// Deciding which accidentals to draw.
//
// An accidental is drawn when a note's spelling differs from what is already in
// force for its line, and not otherwise. Two things put an alteration in force:
// the key signature, which applies to a step name in every octave, and an
// earlier accidental in the same measure, which applies to **one** step in
// **one** octave and expires at the barline.
//
// That second rule is the one worth having in a tested function. "F sharp in
// bar 3 does not need a sharp because bar 3 already had one" is right; "...
// because bar 2 had one" is wrong, and so is "... because the F an octave up
// had one". Both mistakes read as plausible code and produce a page that is
// subtly, consistently misspelled.
namespace score::engraving
{
// The alteration a key signature puts in force for a step, in semitones.
//
// MusicXML writes a key as a count on the circle of fifths, so the order the
// accidentals are added in is fixed: sharps F C G D A E B, flats B E A D G C F.
// Three sharps means the first three of that order are sharp and the rest are
// natural.
[[nodiscard]] int keyAlterationFor(Step step, const KeySignature& key) noexcept;

// The accidental glyph for an alteration, or nothing when none is needed.
[[nodiscard]] std::optional<Glyph> accidentalGlyphFor(int alter) noexcept;

// Running accidental state for one staff.
//
// Reset at each barline, which is what makes a measure the scope of an
// accidental. Key signature state outlives the measure and is set separately.
class AccidentalState
{
  public:
    // Replace the key signature in force. Clears the measure's accidentals too:
    // a key change happens at a barline, and anything still in force belonged
    // to the bar before it.
    void setKey(const KeySignature& key);

    // Called at every barline. The key stays; the measure's accidentals go.
    void beginMeasure();

    // Whether this pitch needs an accidental drawn, and which one -- and
    // records the decision, because a second F sharp in the same bar does not
    // get a second sharp.
    //
    // Not const: asking is what updates the state. Making it two calls would
    // let a caller ask without recording, and a caller that forgot would draw
    // an accidental on every note of a repeated figure.
    [[nodiscard]] std::optional<Glyph> accidentalFor(const Pitch& pitch);

    // What is in force for a pitch's line right now, without recording
    // anything. For tests and for a caller that needs to know rather than to
    // draw.
    [[nodiscard]] int alterationInForceFor(const Pitch& pitch) const noexcept;

  private:
    struct MeasureAccidental
    {
        Step step = Step::c;
        int octave = 4;
        int alter = 0;
    };

    KeySignature keySignature;

    // One entry per altered line, not per note. A measure rarely holds more
    // than a handful.
    std::vector<MeasureAccidental> measureAccidentals;
};
} // namespace score::engraving
