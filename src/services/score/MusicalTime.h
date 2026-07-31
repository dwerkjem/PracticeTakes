#pragma once

#include <cstdint>

// The score model's musical time base (no JUCE dependency, so this is unit
// testable and safe to share with MIDI import).
//
// MusicXML expresses duration in <divisions> units per quarter note, declared
// per part and changeable mid-score. MIDI expresses it in the file's own PPQ.
// Neither is usable directly as a model-wide unit, so every source duration is
// rescaled into one fixed score-wide tick base.
//
// Per design decision 2 (openspec/changes/musicxml-import/design.md), that base
// is 3840 ticks per quarter note. 3840 == 2^8 * 15, so it divides exactly by 2,
// 3, and 5 -- every power of two up to a 256th note, plus triplets and
// quintuplets, land on an integer tick. Consumers get cheap integer arithmetic
// and comparison instead of rational or floating-point durations.
//
// This header is deliberately shared with #34's MIDI timeline: per decision 4
// the two importers produce different models but one time base, so nothing here
// may depend on Score, Part, or Measure.
namespace score
{
// Absolute or relative musical position/duration, in ticks. Signed because
// MusicXML's <backup> moves the write cursor backwards and intermediate
// arithmetic can legitimately go negative before it is validated.
using Tick = std::int64_t;

// The fixed score-wide tick base. Never read a duration without knowing this is
// what it is denominated in.
inline constexpr Tick ticksPerQuarterNote = 3840;

// A whole note is four quarter notes.
inline constexpr Tick ticksPerWholeNote = ticksPerQuarterNote * 4;

// Result of converting one source duration into the model's tick base.
//
// `exact` is the part callers must not ignore: a source whose divisions value
// does not divide 3840 evenly (a 7-tuplet at a fine subdivision, say) rounds,
// and rounding that is silently discarded accumulates across a measure until a
// voice no longer lines up with its barline. Importers turn `exact == false`
// into a diagnostic; they do not drop it.
struct RescaledDuration
{
    Tick ticks = 0;
    bool exact = true;
};

// Convert `sourceDuration`, denominated in `sourceDivisions` units per quarter
// note, into model ticks.
//
// Rounds half away from zero when the conversion is not exact, which keeps the
// error bounded at half a tick (1/7680 of a quarter note) per event rather than
// biasing every inexact duration in one direction.
//
// A non-positive `sourceDivisions` is malformed input, not a programming error:
// MusicXML files with <divisions>0</divisions> exist. It yields zero ticks and
// `exact == false` so the caller diagnoses it rather than dividing by zero.
[[nodiscard]] constexpr RescaledDuration
rescaleToTicks(Tick sourceDuration, Tick sourceDivisions) noexcept
{
    if (sourceDivisions <= 0)
    {
        return {0, false};
    }

    const Tick numerator = sourceDuration * ticksPerQuarterNote;
    const Tick quotient = numerator / sourceDivisions;
    const Tick remainder = numerator % sourceDivisions;

    if (remainder == 0)
    {
        return {quotient, true};
    }

    // Round half away from zero. Comparing 2*|remainder| against the divisor
    // avoids any floating-point step.
    const Tick doubledRemainder = remainder < 0 ? -remainder * 2 : remainder * 2;
    const Tick step = numerator < 0 ? -1 : 1;

    return {doubledRemainder >= sourceDivisions ? quotient + step : quotient, false};
}

// Ticks spanned by one bar of `beats`/`beatType` -- e.g. 6/8 is six eighth
// notes, so 6 * (3840 / 2) ticks. Used for a measure's nominal duration, which
// invariant 7 checks each voice against.
//
// Returns zero for a malformed time signature rather than throwing, again
// because the input comes from a file rather than from our own code.
[[nodiscard]] constexpr Tick nominalMeasureTicks(int beats, int beatType) noexcept
{
    if (beats <= 0 || beatType <= 0)
    {
        return 0;
    }

    return static_cast<Tick>(beats) * ticksPerWholeNote / static_cast<Tick>(beatType);
}
} // namespace score
