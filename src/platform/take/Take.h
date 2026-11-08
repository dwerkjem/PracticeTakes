#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../midi/MidiEvent.h"
#include "../score/Score.h"
#include "../transport/ClockMath.h"
#include "../transport/ClockRenderer.h"
#include "TakeLifecycle.h"

// One take: what was played or sung, on the practice clock's timeline, with
// everything a later analysis needs to read it against the score (no JUCE
// dependency). openspec/changes/archive/2026-11-08-takes/design.md, decision 5.
//
// Positions are clock frames: the frame of the audio callback that captured the
// microphone, at the take's tempo scale. Frame zero is the start point the take
// was counted in to, so a count-in is negative frames.
namespace take
{
using transport::Frame;

// A key, a release or the pedal, at the clock frame it happened.
struct KeyboardEvent
{
    Frame frame = 0;
    midi::MidiEvent event;
};

struct Take
{
    std::uint64_t id = 0;
    TakeLifecycle lifecycle;

    // The score it was played against, held so it outlives being replaced
    // (score::CurrentScore says why that is safe), and its title for display.
    std::shared_ptr<const score::Score> score;
    std::string scoreTitle;

    // Where it was started from, at what tempo scale, and the clock's table at
    // that scale: what turns a frame into a tick.
    score::Tick startTick = 0;
    int scalePercent = transport::defaultScalePercent;
    std::shared_ptr<const transport::ClockTable> table;
    double sampleRate = 44100.0;

    // The device's stated latencies, in frames, for #38 to correct by.
    int inputLatencyFrames = 0;
    int outputLatencyFrames = 0;

    bool usedMicrophone = false;
    bool usedKeyboard = false;

    // The microphone, as the analysis tools hear it: after gain, after mute.
    // `audioStart` is the clock frame of `audio.front()`.
    Frame audioStart = 0;
    std::vector<float> audio;

    std::vector<KeyboardEvent> keyboard;

    // How much of the audio was the count-in, and the last clock frame the take
    // reached: its end.
    Frame countInFrames = 0;
    Frame lastFrame = 0;

    // The zero-based bar a stopped-early take stopped in.
    std::optional<std::int64_t> stoppedInBar;

    [[nodiscard]] TakeState state() const noexcept
    {
        return lifecycle.state();
    }

    // The audio's last frame, exclusive.
    [[nodiscard]] Frame audioEnd() const noexcept
    {
        return audioStart + static_cast<Frame>(audio.size());
    }

    // How long was played, count-in excluded, in seconds.
    [[nodiscard]] double performedSeconds() const noexcept
    {
        return sampleRate > 0.0 ? static_cast<double>(std::max<Frame>(0, lastFrame)) / sampleRate
                                : 0.0;
    }

    // The score tick at a clock frame, at the take's own scale.
    [[nodiscard]] double tickAt(Frame frame) const noexcept
    {
        return table != nullptr ? transport::tickAtFrame(*table, frame) : 0.0;
    }
};

// The clock frame at a time on the high-resolution clock MIDI events are
// stamped on, through the (frame, time) pair the audio callback published.
[[nodiscard]] inline Frame frameAtSeconds(const transport::ClockTiming& timing, double seconds)
{
    return timing.frameAt(seconds * 1000.0);
}
} // namespace take
