#pragma once

#include <cstddef>
#include <string>

#include "../score/Score.h"
#include "PlaybackEvents.h"

// What the Mix popup calls a track, as data (no JUCE dependency); the words
// are l10n::trackName's.
// openspec/changes/archive/2026-11-08-score-playback/design.md, decision 6.
//
// A two-staff part is a piano's two hands, which is #201's staff-is-hand
// assumption; any more staves are numbered. A one-staff part is its name.
namespace transport
{
struct TrackLabel
{
    // The part's name as the file gives it; empty when it gives none.
    std::string partName;

    // One-based, for a part with no name.
    std::size_t partNumber = 1;

    int staff = 1;
    int staves = 1;
};

[[nodiscard]] inline TrackLabel trackLabel(const score::Score& score, const PlaybackTrack& track)
{
    TrackLabel label;
    label.partNumber = track.part + 1;
    label.staff = track.staff;
    if (track.part < score.parts.size())
    {
        const score::Part& source = score.parts[track.part];
        label.partName = source.name;
        label.staves = source.staffCount;
    }
    return label;
}
} // namespace transport
