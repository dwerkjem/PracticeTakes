#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "PlaybackEvents.h"

// What of the score is heard: playback on or off, its volume, and mute and solo
// per track (no JUCE dependency). openspec/changes/archive/2026-11-08-score-playback/design.md,
// decision 5.
//
// The renderer never sees this: it reads the one mask `audibleTracks` folds it
// into, atomically, each block.
namespace transport
{
inline constexpr float defaultPlaybackVolume = 0.8f;

class PlaybackMix
{
  public:
    // A new score's tracks: nothing muted or soloed. Playback and its volume
    // are kept.
    void setTrackCount(std::size_t count)
    {
        const std::size_t tracks = std::min(count, maximumPlaybackTracks);
        muted.assign(tracks, false);
        soloed.assign(tracks, false);
    }

    [[nodiscard]] std::size_t trackCount() const noexcept
    {
        return muted.size();
    }

    bool enabled = false;
    float volume = defaultPlaybackVolume;

    void setMuted(std::size_t track, bool mute)
    {
        if (track < muted.size())
        {
            muted[track] = mute;
        }
    }

    void setSoloed(std::size_t track, bool solo)
    {
        if (track < soloed.size())
        {
            soloed[track] = solo;
        }
    }

    [[nodiscard]] bool isMuted(std::size_t track) const noexcept
    {
        return track < muted.size() && muted[track];
    }

    [[nodiscard]] bool isSoloed(std::size_t track) const noexcept
    {
        return track < soloed.size() && soloed[track];
    }

    // Bit n set when track n sounds: playback is on, the track is not muted,
    // and nothing is soloed or it is.
    [[nodiscard]] std::uint64_t audibleTracks() const noexcept
    {
        if (!enabled)
        {
            return 0;
        }
        const bool anySolo = std::find(soloed.begin(), soloed.end(), true) != soloed.end();
        std::uint64_t mask = 0;
        for (std::size_t track = 0; track < muted.size(); ++track)
        {
            if (!muted[track] && (!anySolo || soloed[track]))
            {
                mask |= std::uint64_t{1} << track;
            }
        }
        return mask;
    }

  private:
    std::vector<bool> muted;
    std::vector<bool> soloed;
};
} // namespace transport
