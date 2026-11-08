#pragma once

#include <algorithm>
#include <cmath>

// The application's tuning reference: A4 in hertz (no JUCE dependency).
// openspec/changes/archive/2026-11-08-system-wide-target-note/design.md, decision 1.
//
// One value, owned by the application, so a tuner set to A442 and anything else
// that turns a frequency into a note agree. The conversions take it as a
// parameter, so nothing converts without one.
namespace pitch
{
inline constexpr double defaultReferenceHz = 440.0;

// Baroque A415 to a semitone above A440, in whole hertz.
inline constexpr double minimumReferenceHz = 415.0;
inline constexpr double maximumReferenceHz = 466.0;

inline constexpr int referenceMidiNote = 69;

[[nodiscard]] inline double clampReference(double hertz) noexcept
{
    if (!std::isfinite(hertz))
    {
        return defaultReferenceHz;
    }
    return std::clamp(std::round(hertz), minimumReferenceHz, maximumReferenceHz);
}

// MIDI numbers, fractional, at a reference.
[[nodiscard]] inline double frequencyToMidi(double frequency, double referenceHz) noexcept
{
    return referenceMidiNote + (12.0 * std::log2(frequency / referenceHz));
}

[[nodiscard]] inline double midiToFrequency(double midiPitch, double referenceHz) noexcept
{
    return referenceHz * std::pow(2.0, (midiPitch - referenceMidiNote) / 12.0);
}

// The reference the application uses. Message thread; a tool reads it on its
// own timer, as it reads the current score.
class ReferencePitch
{
  public:
    [[nodiscard]] double hertz() const noexcept
    {
        return value;
    }

    void set(double hertz) noexcept
    {
        value = clampReference(hertz);
    }

  private:
    double value = defaultReferenceHz;
};
} // namespace pitch
