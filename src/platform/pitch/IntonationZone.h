#pragma once

#include <algorithm>
#include <cmath>

// Which of four zones a pitch is in, against the note it aims at (no JUCE
// dependency). One rule, so the Note View's live colours and a take review's
// marks agree: green when near perfect, blue inside the singer's acceptable
// range, amber when near but off, red outside.
namespace pitch
{
// Near perfect, by default: the tuner's in-tune band. The singer can narrow or
// widen it in Settings, between these limits.
inline constexpr double nearPerfectCents = 5.0;
inline constexpr double minimumNearPerfectCents = 1.0;
inline constexpr double maximumNearPerfectCents = 20.0;

// Past this the pitch is nearer another note.
inline constexpr double nearNoteCents = 50.0;

enum class IntonationZone
{
    nearPerfect,
    acceptable,
    nearButOff,
    outside
};

// The two limits a singer sets, in cents either way: what counts as near
// perfect, and what is still acceptable ("in tune within").
struct ZoneLimits
{
    double nearPerfectCents = pitch::nearPerfectCents;
    double toleranceCents = 25.0;

    [[nodiscard]] bool operator==(const ZoneLimits&) const = default;
};

// The zone of a pitch `cents` from its note, either way, when the acceptable
// range is `toleranceCents` either way and near perfect is `nearPerfect`. A
// tolerance inside the near-perfect band leaves no acceptable zone.
[[nodiscard]] inline IntonationZone
zoneOf(double cents, double toleranceCents, double nearPerfect = nearPerfectCents) noexcept
{
    const double off = std::abs(cents);
    if (off <= nearPerfect)
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

// Each band sits strictly inside the next looser one: near perfect inside in
// tune within, in tune within inside the near-note limit, at least this far
// apart -- one whole cent, the Settings step.
inline constexpr double bandGapCents = 1.0;
inline constexpr double minimumToleranceCents = minimumNearPerfectCents + bandGapCents;
inline constexpr double maximumToleranceCents = nearNoteCents - bandGapCents;

// The limits with that order restored. The tolerance is kept first, inside its
// range, because whether a take's note is in tune depends on it; near perfect
// then gives way to it. A value that is not a number takes its default.
[[nodiscard]] inline ZoneLimits orderedLimits(ZoneLimits limits) noexcept
{
    const ZoneLimits defaults;
    const double tolerance =
        std::isfinite(limits.toleranceCents) ? limits.toleranceCents : defaults.toleranceCents;
    const double nearPerfect = std::isfinite(limits.nearPerfectCents) ? limits.nearPerfectCents
                                                                      : defaults.nearPerfectCents;

    ZoneLimits result;
    result.toleranceCents = std::clamp(tolerance, minimumToleranceCents, maximumToleranceCents);
    result.nearPerfectCents = std::clamp(
        nearPerfect, minimumNearPerfectCents,
        std::min(maximumNearPerfectCents, result.toleranceCents - bandGapCents));
    return result;
}

[[nodiscard]] inline IntonationZone zoneOf(double cents, ZoneLimits limits) noexcept
{
    return zoneOf(cents, limits.toleranceCents, limits.nearPerfectCents);
}
} // namespace pitch
