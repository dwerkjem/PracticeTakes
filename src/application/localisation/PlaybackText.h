#pragma once

#include "../../platform/transport/PlaybackTrackNames.h"
#include "Localisation.h"

namespace l10n
{
// What the Mix popup calls a track: the part's name, or "Part 3"; and for a
// part on two staves its hand, on more its staff.
[[nodiscard]] inline juce::String trackName(const transport::TrackLabel& label)
{
    const juce::String part =
        label.partName.empty()
            ? l10n::format(
                  "playback.track.part",
                  {{"number", l10n::number(static_cast<double>(label.partNumber), 0)}})
            : juce::String::fromUTF8(label.partName.c_str());
    if (label.staves <= 1)
    {
        return part;
    }
    if (label.staves == 2)
    {
        return l10n::format(
            label.staff == 1 ? l10n::key("playback.track.right-hand")
                             : l10n::key("playback.track.left-hand"),
            {{"part", part}});
    }
    return l10n::format(
        "playback.track.staff",
        {{"part", part}, {"staff", l10n::number(static_cast<double>(label.staff), 0)}});
}
} // namespace l10n
