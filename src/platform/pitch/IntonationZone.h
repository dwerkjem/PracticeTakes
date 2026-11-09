#pragma once

#include <algorithm>
#include <cmath>

// Which of four zones a pitch is in, against the note it aims at (no JUCE
// dependency). One rule, so the Note View's live colours, the tuner's bands and
// a take review's marks agree: green when near perfect, blue inside the
// singer's acceptable range, amber when near but off, red when out of tune.
namespace pitch
{
// Near perfect, by default: the tuner's in-tune band. The singer can narrow or
// widen it in Settings, between these limits.
inline constexpr double nearPerfectCents = 5.0;
inline constexpr double minimumNearPerfectCents = 1.0;
inline constexpr double maximumNearPerfectCents = 20.0;

// Past this the pitch is nearer another note. The out-of-tune limit can be no
// wider.
inline constexpr double nearNoteCents = 50.0;

// Out of tune -- red -- past this many cents either way, by default.
inline constexpr double outOfTuneCents = 40.0;

enum class IntonationZone
{
    nearPerfect,
    acceptable,
    nearButOff,
    outside
};

// The three limits a singer sets, in cents either way (plus or minus): what
// counts as near perfect, what is still acceptable ("in tune within"), and
// past what a note is out of tune ("out of tune beyond"), where amber turns
// red.
struct ZoneLimits
{
    double nearPerfectCents = pitch::nearPerfectCents;
    double toleranceCents = 25.0;
    double outOfTuneCents = pitch::outOfTuneCents;

    [[nodiscard]] bool operator==(const ZoneLimits&) const = default;
};

// The zone of a pitch `cents` from its note, either way, when near perfect is
// within `nearPerfect`, acceptable within `toleranceCents`, and out of tune
// past `outOfTune`. Limits out of order leave the zones between them empty;
// pitch::orderedLimits keeps them in order.
[[nodiscard]] inline IntonationZone zoneOf(
    double cents,
    double toleranceCents,
    double nearPerfect = nearPerfectCents,
    double outOfTune = outOfTuneCents) noexcept
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
    if (off <= outOfTune)
    {
        return IntonationZone::nearButOff;
    }
    return IntonationZone::outside;
}

// Each band sits strictly inside the next looser one -- near perfect inside in
// tune within, in tune within inside out of tune beyond -- at least this far
// apart: one whole cent, the Settings step. Out of tune beyond goes no further
// than the near-note limit.
inline constexpr double bandGapCents = 1.0;
inline constexpr double minimumToleranceCents = minimumNearPerfectCents + bandGapCents;
inline constexpr double maximumToleranceCents = nearNoteCents - bandGapCents;
inline constexpr double minimumOutOfTuneCents = minimumToleranceCents + bandGapCents;
inline constexpr double maximumOutOfTuneCents = nearNoteCents;

// The limits with that order restored. The tolerance is kept first, inside its
// range, because whether a take's note is in tune depends on it; near perfect
// and out of tune then give way to it, each on its own side. A value that is
// not a number takes its default.
[[nodiscard]] inline ZoneLimits orderedLimits(ZoneLimits limits) noexcept
{
    const ZoneLimits defaults;
    const auto orDefault = [](double value, double fallback)
    { return std::isfinite(value) ? value : fallback; };

    ZoneLimits result;
    result.toleranceCents = std::clamp(
        orDefault(limits.toleranceCents, defaults.toleranceCents), minimumToleranceCents,
        maximumToleranceCents);
    result.nearPerfectCents = std::clamp(
        orDefault(limits.nearPerfectCents, defaults.nearPerfectCents), minimumNearPerfectCents,
        std::min(maximumNearPerfectCents, result.toleranceCents - bandGapCents));
    result.outOfTuneCents = std::clamp(
        orDefault(limits.outOfTuneCents, defaults.outOfTuneCents),
        result.toleranceCents + bandGapCents, maximumOutOfTuneCents);
    return result;
}

[[nodiscard]] inline IntonationZone zoneOf(double cents, ZoneLimits limits) noexcept
{
    return zoneOf(cents, limits.toleranceCents, limits.nearPerfectCents, limits.outOfTuneCents);
}
} // namespace pitch
