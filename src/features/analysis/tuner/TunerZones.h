#pragma once

#include "../../../platform/pitch/IntonationZone.h"

#include <cmath>
#include <limits>
#include <optional>
#include <vector>

// The intonation zones the tuner's bar and dial draw (no JUCE dependency), so
// the rule for what is lit can be asserted without a display.
//
// The zones nest around 0 cents, each as wide as its own range: near perfect
// out to the singer's near-perfect limit, acceptable out to their "in tune
// within", near-but-off out to their "out of tune beyond", and outside -- red
// -- everything beyond that, to the edge of whatever draws it. The limits are put
// in order first (pitch::orderedLimits), so each band sits strictly inside the
// next and all four are always there.
//
// The zone the pitch is in is lit. The tighter zones inside it stay faintly
// tinted, so the target is always visible; the looser zones outside it are
// turned off and leave only a dotted line where each begins (its inner edge),
// in its own colour -- so no two lines fall on the same place, and red's
// begins where the singer's out-of-tune limit is. With no signal
// nothing is lit and every zone is tinted.
//
// Only the half of the lit zone the pitch is on lights -- the sharp half or
// the flat half -- and it carries a sharp or flat sign, so which way to go is
// as plain as how far. The other half of it stays tinted. Dead on zero both
// halves light. On pitch -- near perfect -- there is no sign: there is nothing
// to correct.
namespace tuner
{
enum class BandLook
{
    lit,
    tinted,
    dotted
};

// Which half of a lit band is lit: below the note, above it, or both.
enum class Side
{
    both,
    flat,
    sharp
};

struct ZoneBand
{
    pitch::IntonationZone zone;

    // The band covers innerCents..outerCents on each side of 0; the
    // near-perfect band's inner edge is 0, and the outside band's outer edge
    // is infinite: a display draws it to its own edge.
    double innerCents;
    double outerCents;

    BandLook look;

    // For a lit band, which half; both otherwise.
    Side litSide = Side::both;
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

// The four bands to draw, tightest first. `cents` is the reading against its
// note, or nothing when there is no signal.
[[nodiscard]] inline std::vector<ZoneBand>
zoneBands(std::optional<double> cents, pitch::ZoneLimits requested)
{
    const auto limits = pitch::orderedLimits(requested);

    std::vector<ZoneBand> bands{
        {pitch::IntonationZone::nearPerfect, 0.0, limits.nearPerfectCents, BandLook::tinted},
        {pitch::IntonationZone::acceptable, limits.nearPerfectCents, limits.toleranceCents,
         BandLook::tinted},
        {pitch::IntonationZone::nearButOff, limits.toleranceCents, limits.outOfTuneCents,
         BandLook::tinted},
        {pitch::IntonationZone::outside, limits.outOfTuneCents,
         std::numeric_limits<double>::infinity(), BandLook::tinted}};

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
        if (band.look == BandLook::lit)
        {
            band.litSide = *cents > 0.0 ? Side::sharp : *cents < 0.0 ? Side::flat : Side::both;
        }
    }
    return bands;
}

// Whether this band carries the sharp or flat sign: the lit one, off pitch,
// with a side to it.
[[nodiscard]] inline bool carriesSign(const ZoneBand& band) noexcept
{
    return band.look == BandLook::lit && band.litSide != Side::both &&
           band.zone != pitch::IntonationZone::nearPerfect;
}
} // namespace tuner
