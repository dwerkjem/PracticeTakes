#pragma once

#include "../../../platform/pitch/IntonationZone.h"

#include <cmath>
#include <optional>
#include <vector>

// The intonation zones the tuner's bar and dial draw (no JUCE dependency), so
// the rule for what is lit can be asserted without a display.
//
// The zones nest around 0 cents, each as wide as its own range: near perfect
// out to the singer's near-perfect limit, acceptable out to their "in tune
// within", near-but-off out to pitch::nearNoteCents. The limits are put in
// order first (pitch::orderedLimits), so each band sits strictly inside the
// next and all three are always there.
//
// The zone the pitch is in is lit. The tighter zones inside it stay faintly
// tinted, so the target is always visible; the looser zones outside it are
// turned off and leave only a dotted line at their edge. With no signal
// nothing is lit and every zone is tinted; outside every zone, likewise.
namespace tuner
{
enum class BandLook
{
    lit,
    tinted,
    dotted
};

struct ZoneBand
{
    pitch::IntonationZone zone;

    // The band covers innerCents..outerCents on each side of 0; the
    // near-perfect band's inner edge is 0.
    double innerCents;
    double outerCents;

    BandLook look;
};

[[nodiscard]] inline int precisionRank(pitch::IntonationZone zone) noexcept
{
    switch (zone)
    {
    case pitch::IntonationZone::nearPerfect:
        return 0;
    case pitch::IntonationZone::acceptable:
        return 1;
    case pitch::IntonationZone::nearButOff:
        return 2;
    case pitch::IntonationZone::outside:
        return 3;
    }
    return 3;
}

// The three bands to draw, tightest first. `cents` is the reading against its
// note, or nothing when there is no signal.
[[nodiscard]] inline std::vector<ZoneBand>
zoneBands(std::optional<double> cents, pitch::ZoneLimits requested)
{
    const auto limits = pitch::orderedLimits(requested);

    std::vector<ZoneBand> bands{
        {pitch::IntonationZone::nearPerfect, 0.0, limits.nearPerfectCents, BandLook::tinted},
        {pitch::IntonationZone::acceptable, limits.nearPerfectCents, limits.toleranceCents,
         BandLook::tinted},
        {pitch::IntonationZone::nearButOff, limits.toleranceCents, pitch::nearNoteCents,
         BandLook::tinted}};

    if (!cents.has_value() || !std::isfinite(*cents))
    {
        return bands;
    }

    const int current = precisionRank(pitch::zoneOf(*cents, limits));
    for (auto& band : bands)
    {
        const int rank = precisionRank(band.zone);
        band.look = rank == current  ? BandLook::lit
                    : rank < current ? BandLook::tinted
                                     : BandLook::dotted;
    }
    return bands;
}
} // namespace tuner
