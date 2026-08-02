#pragma once

#include <optional>
#include <string>
#include <vector>

#include "MusicalTime.h"

// Notated attributes that apply from a point in the score onwards: clef, key,
// time signature, and the tempo/dynamic directions attached to a position.
//
// These are *notation*, not engraving. A clef says "this staff reads in bass
// clef"; it does not say where on the page the clef glyph is drawn. Invariant
// 10 keeps the latter out of the model entirely.
namespace score
{
// MusicXML writes a clef as a sign plus the staff line it sits on, which is
// more general than an enum of named clefs and costs nothing to keep verbatim:
// it covers treble, bass, alto, tenor, percussion, and the octave-transposed
// vocal tenor clef without a special case for each.
struct Clef
{
    // MusicXML <sign>: "G", "F", "C", "percussion", "TAB", "none".
    std::string sign = "G";

    // Staff line the sign centres on, counting from the bottom line as 1.
    // Treble is G on line 2; bass is F on line 4.
    int line = 2;

    // <clef-octave-change>: -1 is the tenor clef that sounds an octave below
    // where it reads. Affects sounding pitch, so playback must not ignore it.
    int octaveChange = 0;

    // Which staff of a multi-staff part this clef governs. MusicXML numbers
    // staves from 1.
    int staff = 1;
};

// A key signature, as a count of accidentals on the circle of fifths.
struct KeySignature
{
    // MusicXML <fifths>: positive is sharps, negative is flats. 0 is C major
    // or A minor; 3 is A major; -2 is B-flat major.
    int fifths = 0;

    // MusicXML <mode>: "major", "minor", or empty when the file does not say.
    // Kept because a renderer may label it and because fifths alone cannot
    // distinguish a key from its relative.
    std::string mode;
};

struct TimeSignature
{
    int beats = 4;
    int beatType = 4;
};

// Ticks in one bar of this time signature -- a measure's nominal duration,
// which invariant 7 checks each voice against.
[[nodiscard]] constexpr Tick nominalTicks(const TimeSignature& time) noexcept
{
    return nominalMeasureTicks(time.beats, time.beatType);
}

// Attribute changes taking effect at the start of a measure. All optional
// because most measures change nothing; a measure with no attributes inherits
// whatever was in force.
struct AttributeChanges
{
    // One entry per staff that changes clef. A piano part changing only its
    // bass staff has one entry here, not two.
    std::vector<Clef> clefs;

    std::optional<KeySignature> key;
    std::optional<TimeSignature> time;
};

[[nodiscard]] inline bool isEmpty(const AttributeChanges& changes) noexcept
{
    return changes.clefs.empty() && !changes.key.has_value() && !changes.time.has_value();
}

enum class DirectionKind
{
    // A tempo marking, from <sound tempo="..."> or <metronome>.
    tempo,

    // A dynamic marking: pp, mf, sfz, and so on.
    dynamic
};

// A tempo or dynamic marking attached to a position in a part.
//
// Dynamics are treated as notation attached to a position, not as playback
// velocity -- that reading of "basic dynamics" is recorded as an assumption in
// design.md. Turning "mf" into a gain is playback's job (#35-#38), and doing it
// here would bake one interpretation into the shared model.
struct Direction
{
    DirectionKind kind = DirectionKind::dynamic;

    // Onset relative to the start of the containing measure, in model ticks,
    // matching ScoreEvent::onset.
    Tick onset = 0;

    // Which staff the marking sits against, for a multi-staff part.
    int staff = 1;

    // For `dynamic`: the marking as written ("mf", "sfz"). For `tempo`: the
    // text of the metronome marking if the file gave one ("Allegro"), otherwise
    // empty.
    std::string text;

    // For `tempo` only: quarter notes per minute. Unset on a dynamic.
    std::optional<double> beatsPerMinute;
};
} // namespace score
