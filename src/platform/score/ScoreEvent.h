#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "LyricSyllable.h"
#include "MusicalTime.h"
#include "Pitch.h"

// One thing that happens in a voice: a note, a chord, or a rest.
//
// A chord is *one* event carrying several pitches, not several simultaneous
// events. That is invariant 2's doing: "no two events in a voice overlap" is
// only a meaningful rule if a chord does not count as an overlap with itself.
// It also matches how MusicXML writes them -- a run of <note> elements where
// every note after the first carries <chord/> and consumes no extra time -- and
// how every consumer wants to read them.
namespace score
{
enum class EventKind
{
    note,
    chord,
    rest
};

// Identifies one note within a part, for tie linkage.
//
// Down to the note rather than the event, because **a tie is a property of a
// note, not of a chord**. A pianist holding the bass of a chord while the upper
// voices move is writing exactly that, and it is common rather than exotic: in
// one real guitar score in the test corpus, 44 of 256 chords tie only some of
// their notes. An event-level tie cannot express it, and flattening it to "the
// whole chord is tied" is silently wrong in both directions -- it sustains
// notes that were re-struck and drops ties that were written.
//
// A tie can cross a barline, so the measure index is part of the identity.
struct NoteRef
{
    std::size_t measureIndex = 0;
    std::size_t voiceIndex = 0;
    std::size_t eventIndex = 0;

    // Which note of the event's chord. Zero for a single note.
    std::size_t noteIndex = 0;
};

[[nodiscard]] constexpr bool operator==(const NoteRef& lhs, const NoteRef& rhs) noexcept
{
    return lhs.measureIndex == rhs.measureIndex && lhs.voiceIndex == rhs.voiceIndex &&
           lhs.eventIndex == rhs.eventIndex && lhs.noteIndex == rhs.noteIndex;
}

[[nodiscard]] constexpr bool operator!=(const NoteRef& lhs, const NoteRef& rhs) noexcept
{
    return !(lhs == rhs);
}

// One sounding note: its pitch, and where it ties.
//
// Set on the note a tie *starts* at, pointing to where it stops, and vice
// versa. Invariant 4 requires both ends to exist, refer to a real note, and
// sound the same pitch; unmatched ends are dropped with a diagnostic rather
// than left dangling, because a half-linked tie is a null dereference waiting
// to happen in every consumer.
//
// This is the sounding tie (MusicXML <tie>), never the visual slur (<slur>)
// and never its engraved counterpart (<notations><tied>). All three look alike
// on the page and are routinely confused, which is why the importer tests for
// it explicitly.
struct Note
{
    Pitch pitch;

    std::optional<NoteRef> tiedTo;
    std::optional<NoteRef> tiedFrom;
};

// The ratio a tuplet compresses time by: `actual` notes played in the time of
// `normal`. A triplet is {3, 2}. The *bracket* a notation program draws is
// engraving data and invariant 10 keeps it out of the model; the ratio is not,
// because it is what makes the duration arithmetic add up.
struct TupletRatio
{
    int actual = 1;
    int normal = 1;
};

[[nodiscard]] constexpr bool isPlainTuplet(const TupletRatio& ratio) noexcept
{
    return ratio.actual == 1 && ratio.normal == 1;
}

struct ScoreEvent
{
    EventKind kind = EventKind::rest;

    // Which staff of a multi-staff part this sits on. A piano part has staff 1
    // (treble) and staff 2 (bass); a vocal part has only staff 1. MusicXML
    // numbers staves from 1.
    int staff = 1;

    // Onset relative to the start of the containing measure, in model ticks.
    // Measure-relative rather than absolute so that a measure can be examined,
    // tested, and repaired on its own; the absolute position is the measure's
    // start plus this.
    Tick onset = 0;

    // Duration in model ticks. Zero only for grace notes (invariant 3).
    Tick duration = 0;

    // A grace note: an ornamental note that takes its time from performance
    // rather than from the bar's arithmetic. Modelled as a zero-duration event
    // that does not advance the voice cursor, which is enough for the renderer
    // to draw it and enough for playback to ignore it for now.
    bool isGrace = false;

    TupletRatio tuplet;

    // Empty for a rest, one entry for a note, two or more for a chord. Each
    // carries its own tie linkage; see Note.
    std::vector<Note> notes;

    // Lyrics attached to this event. Multiple entries mean multiple verses, not
    // multiple syllables in sequence. Empty on a rest and on most instrumental
    // notes.
    std::vector<LyricSyllable> lyrics;
};

// Where this event ends, relative to the measure start.
[[nodiscard]] constexpr Tick endOf(const ScoreEvent& event) noexcept
{
    return event.onset + event.duration;
}

[[nodiscard]] inline bool isPitched(const ScoreEvent& event) noexcept
{
    return event.kind != EventKind::rest && !event.notes.empty();
}

// Whether any note of this event ties, in either direction. A chord may tie
// some of its notes and not others, so this is a question about the event and
// `Note::tiedTo`/`tiedFrom` is the question about a note.
[[nodiscard]] inline bool hasAnyTie(const ScoreEvent& event) noexcept
{
    for (const Note& note : event.notes)
    {
        if (note.tiedTo.has_value() || note.tiedFrom.has_value())
        {
            return true;
        }
    }

    return false;
}
} // namespace score
