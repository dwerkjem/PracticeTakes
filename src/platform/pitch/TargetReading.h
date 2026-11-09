#pragma once

#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>

#include "CurrentTarget.h"
#include "IntonationZone.h"

// Where the note the tuner hears stands against the target (no JUCE
// dependency). Said in words by tuner::targetReadingText.
// openspec/changes/archive/2026-11-08-system-wide-target-note/design.md, decision 4.
namespace pitch
{
// The tuner's own in-tune band by default, so "on it" and the tuner's green
// agree; the tuner passes the singer's near-perfect limit.
inline constexpr double onTargetCents = nearPerfectCents;

enum class TargetStanding
{
    unheard,
    onIt,
    sharp,
    flat,
    above,
    below
};

struct TargetReading
{
    TargetStanding standing = TargetStanding::unheard;

    // Cents for sharp and flat, semitones for above and below; zero otherwise.
    long amount = 0;
};

// `heardMidi` is the pitch being heard as a fractional MIDI number, or none
// while nothing is. Within `onCents` either way is on it.
[[nodiscard]] inline TargetReading readAgainstTarget(
    const Target& target,
    std::optional<double> heardMidi,
    double onCents = onTargetCents)
{
    TargetReading reading;
    if (!heardMidi.has_value() || !std::isfinite(*heardMidi))
    {
        return reading;
    }

    const double semitones = *heardMidi - static_cast<double>(target.midiNoteNumber);
    const double cents = semitones * 100.0;
    if (std::abs(cents) <= onCents)
    {
        reading.standing = TargetStanding::onIt;
    }
    else if (std::abs(semitones) < 0.5)
    {
        reading.standing = cents > 0.0 ? TargetStanding::sharp : TargetStanding::flat;
        reading.amount = std::lround(std::abs(cents));
    }
    else
    {
        reading.standing = semitones > 0.0 ? TargetStanding::above : TargetStanding::below;
        reading.amount = std::lround(std::abs(semitones));
    }
    return reading;
}
} // namespace pitch
