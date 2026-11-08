#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../score/Score.h"
#include "ClockMath.h"

// The score as notes on the practice clock's timeline, for playback (no JUCE
// dependency). openspec/changes/archive/2026-11-08-score-playback/design.md, decision 3.
//
// Built on the message thread whenever the clock's table is, into the slot the
// renderer is not reading, so the notes and the table always agree.
namespace transport
{
// One synthesiser channel per track, and a 64-bit mask of them.
inline constexpr std::size_t maximumPlaybackTracks = 64;

// The General MIDI program a part plays when its file names none: an acoustic
// grand piano.
inline constexpr int defaultMidiProgram = 0;

// A staff of a part: what is muted or soloed, and what a hand is.
struct PlaybackTrack
{
    std::size_t part = 0;

    // 1-based, as the model counts staves.
    int staff = 1;

    int program = defaultMidiProgram;
};

struct PlaybackEvent
{
    Frame frame = 0;
    std::uint8_t track = 0;
    std::uint8_t key = 60;
    bool on = false;
};

struct PlaybackScore
{
    std::vector<PlaybackTrack> tracks;

    // In time order; at the same frame, offs before ons, so a repeated note
    // strikes again.
    std::vector<PlaybackEvent> events;
};

// Every staff of every part, in score order, up to `maximumPlaybackTracks`.
[[nodiscard]] std::vector<PlaybackTrack> playbackTracksOf(const score::Score& score);

// Fill `into` with the score's notes at `table`'s frames. A pitched, non-grace
// note sounds from its onset to the end of the last note tied to it; the tie's
// continuations make no event. A note on a track past the maximum is not
// played. `into`'s storage is reused.
void playbackEventsOf(const score::Score& score, const ClockTable& table, PlaybackScore& into);
} // namespace transport
