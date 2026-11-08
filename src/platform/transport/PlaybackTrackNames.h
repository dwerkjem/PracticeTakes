#pragma once

#include <string>

#include "../score/Score.h"
#include "PlaybackEvents.h"

// What the Mix popup calls a track (no JUCE dependency).
// openspec/changes/score-playback/design.md, decision 6.
//
// A two-staff part is a piano's two hands, which is #201's staff-is-hand
// assumption; any more staves are numbered. A one-staff part is its name.
namespace transport
{
[[nodiscard]] inline std::string trackName(const score::Score& score, const PlaybackTrack& track)
{
    std::string part = "Part " + std::to_string(track.part + 1);
    int staves = 1;
    if (track.part < score.parts.size())
    {
        const score::Part& source = score.parts[track.part];
        if (!source.name.empty())
        {
            part = source.name;
        }
        staves = source.staffCount;
    }

    if (staves <= 1)
    {
        return part;
    }
    if (staves == 2)
    {
        return part + (track.staff == 1 ? ", right hand" : ", left hand");
    }
    return part + ", staff " + std::to_string(track.staff);
}
} // namespace transport
