#pragma once

#include <vector>

#include "MusicalTime.h"

// Conversion between musical position (ticks) and wall-clock seconds.
//
// Per decision 4 this is the *only* piece the MusicXML score model and #34's
// MIDI timeline share, so it deliberately knows nothing about Score, Part, or
// Measure -- just ticks and tempo. Keeping it standalone is what lets MIDI
// import reuse it instead of reinventing the arithmetic.
//
// Not real-time safe and not meant to be: per the ownership rules in
// design.md, the audio thread never reads the score model. Playback flattens
// what it needs into a preallocated POD array on the message thread first.
namespace score
{
// The tempo assumed when a file declares none. MusicXML does not require a
// tempo marking and plenty of engraved vocal scores have none; 120 BPM is the
// conventional default and invariant 8 guarantees the map is never empty, so
// consumers never have to handle "no tempo".
inline constexpr double defaultBeatsPerMinute = 120.0;

struct TempoEntry
{
    // Absolute position in the score, in model ticks.
    Tick tick = 0;

    // Quarter notes per minute from this tick until the next entry.
    double beatsPerMinute = defaultBeatsPerMinute;
};

// Ordered tempo entries with position/seconds conversion in both directions.
//
// Invariant 8: non-empty, sorted by tick, no duplicate ticks. `build` enforces
// all three, so a constructed TempoMap is always usable.
class TempoMap
{
  public:
    // Default: a single 120 BPM entry at tick 0.
    TempoMap();

    // Build from raw entries, applying invariant 8.
    //
    // Repairs rather than rejects, matching the model's general rule that a bad
    // file produces a diagnostic and a usable score, not an exception:
    //  - entries are sorted by tick;
    //  - on duplicate ticks the last one wins, because a later declaration in
    //    the file is the one the engraver meant;
    //  - non-positive or non-finite tempi are dropped as meaningless;
    //  - if nothing is left, or nothing starts at or before tick 0, a default
    //    120 BPM entry is inserted at tick 0.
    [[nodiscard]] static TempoMap build(std::vector<TempoEntry> entries);

    [[nodiscard]] const std::vector<TempoEntry>& entries() const noexcept
    {
        return entries_;
    }

    // Tempo in force at `tick`.
    [[nodiscard]] double beatsPerMinuteAt(Tick tick) const noexcept;

    // Seconds elapsed from the start of the score to `tick`. Accumulates
    // segment by segment, so a mid-score tempo change is exact rather than
    // approximated by the tempo at either end.
    [[nodiscard]] double tickToSeconds(Tick tick) const noexcept;

    // Inverse of `tickToSeconds`. Rounds to the nearest tick; seconds before
    // the start of the score clamp to tick 0.
    [[nodiscard]] Tick secondsToTick(double seconds) const noexcept;

  private:
    explicit TempoMap(std::vector<TempoEntry> entries);

    // Rebuild `secondsAtEntry_` from `entries_`.
    void recomputeCumulativeSeconds();

    std::vector<TempoEntry> entries_;

    // Seconds elapsed at the start of each entry, precomputed so a conversion
    // is a binary search plus one multiply rather than a walk from the top of
    // the score. Same length as `entries_`.
    std::vector<double> secondsAtEntry_;
};

// Seconds that `ticks` ticks occupy at a constant `beatsPerMinute`.
[[nodiscard]] double ticksToSecondsAt(Tick ticks, double beatsPerMinute) noexcept;
} // namespace score
