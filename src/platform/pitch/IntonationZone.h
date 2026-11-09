#pragma once

#include <cmath>

// Which of four zones a pitch is in, against the note it aims at (no JUCE
// dependency). One rule, so the Note View's live colours and a take review's
// marks agree: green when near perfect, blue inside the singer's acceptable
// range, amber when near but off, red outside.
namespace pitch
{
// Near perfect: the tuner's in-tune band.
inline constexpr double nearPerfectCents = 5.0;

// Past this the pitch is nearer another note.
inline constexpr double nearNoteCents = 50.0;

enum class IntonationZone
{
    nearPerfect,
    acceptable,
    nearButOff,
    outside
};

// The zone of a pitch `cents` from its note, either way, when the acceptable
// range is `toleranceCents` either way. A tolerance inside the near-perfect
// band leaves no acceptable zone.
[[nodiscard]] inline IntonationZone zoneOf(double cents, double toleranceCents) noexcept
{
    const double off = std::abs(cents);
    if (off <= nearPerfectCents)
    {
        return IntonationZone::nearPerfect;
    }
    if (off <= toleranceCents)
    {
        return IntonationZone::acceptable;
    }
    if (off <= nearNoteCents)
    {
        return IntonationZone::nearButOff;
    }
    return IntonationZone::outside;
}
} // namespace pitch
