#pragma once

#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>

#include "CurrentTarget.h"

// What the tuner says about the note it hears against the target (no JUCE
// dependency).
// openspec/changes/archive/2026-11-08-system-wide-target-note/design.md, decision 4.
namespace pitch
{
// The tuner's own in-tune band, so "on it" and the tuner's green agree.
inline constexpr double onTargetCents = 5.0;

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

    // "Target B3 · 12 ¢ flat".
    std::string text;
};

// `heardMidi` is the pitch being heard as a fractional MIDI number, or none
// while nothing is.
[[nodiscard]] inline TargetReading
readAgainstTarget(const Target& target, std::optional<double> heardMidi)
{
    TargetReading reading;
    reading.text = "Target " + target.name;
    if (!heardMidi.has_value() || !std::isfinite(*heardMidi))
    {
        return reading;
    }

    const double semitones = *heardMidi - static_cast<double>(target.midiNoteNumber);
    const double cents = semitones * 100.0;
    const std::string separator = " \xC2\xB7 ";
    if (std::abs(cents) <= onTargetCents)
    {
        reading.standing = TargetStanding::onIt;
        reading.text += separator + "on it";
    }
    else if (std::abs(semitones) < 0.5)
    {
        reading.standing = cents > 0.0 ? TargetStanding::sharp : TargetStanding::flat;
        reading.amount = std::lround(std::abs(cents));
        reading.text += separator + std::to_string(reading.amount) + " \xC2\xA2 " +
                        (cents > 0.0 ? "sharp" : "flat");
    }
    else
    {
        reading.standing = semitones > 0.0 ? TargetStanding::above : TargetStanding::below;
        reading.amount = std::lround(std::abs(semitones));
        reading.text += separator + std::to_string(reading.amount) +
                        (reading.amount == 1 ? " semitone " : " semitones ") +
                        (semitones > 0.0 ? "above" : "below");
    }
    return reading;
}
} // namespace pitch
