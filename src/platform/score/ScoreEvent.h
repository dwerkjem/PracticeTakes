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

// Identifies an event within a part, for tie linkage. A tie can cross a
// barline, so a measure-relative index is not enough on its own.
struct EventRef
{
    std::size_t measureIndex = 0;
    std::size_t voiceIndex = 0;
    std::size_t eventIndex = 0;
};

[[nodiscard]] constexpr bool operator==(const EventRef& lhs, const EventRef& rhs) noexcept
{
    return lhs.measureIndex == rhs.measureIndex && lhs.voiceIndex == rhs.voiceIndex &&
           lhs.eventIndex == rhs.eventIndex;
}

[[nodiscard]] constexpr bool operator!=(const EventRef& lhs, const EventRef& rhs) noexcept
{
    return !(lhs == rhs);
}

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

    // Empty for a rest, one entry for a note, two or more for a chord.
    std::vector<Pitch> pitches;

    // Set on the event a tie *starts* at, pointing to where it stops, and vice
    // versa. Invariant 4 requires both ends to exist, refer to a real event, and
    // sound the same pitch; unmatched ends are dropped with a diagnostic rather
    // than left dangling.
    //
    // This is the sounding tie (MusicXML <tie>), never the visual slur
    // (<slur>). The two look alike on the page and are routinely confused,
    // which is why the importer tests for it explicitly.
    std::optional<EventRef> tiedTo;
    std::optional<EventRef> tiedFrom;

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
    return event.kind != EventKind::rest && !event.pitches.empty();
}
} // namespace score
