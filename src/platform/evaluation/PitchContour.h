#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// The pitch of a take's audio, frame by frame: YIN over the recorded
// microphone (no JUCE dependency).
// openspec/changes/archive/2026-11-09-sung-pitch-judging/design.md, decision 1.
namespace evaluation
{
struct ContourSettings
{
    // The analysis window, rounded to the nearest power of two in samples
    // (2,048 at 44.1 kHz), and how many hops fit in it.
    double windowSeconds = 0.046;
    std::size_t hopsPerWindow = 4;

    // A bass's low E to above a soprano's high C.
    double minimumHz = 60.0;
    double maximumHz = 1500.0;

    // YIN's absolute threshold: the first dip of the normalised difference
    // below it is the period.
    double threshold = 0.15;

    // Quieter than this is silence.
    double minimumRms = 0.005;
};

struct ContourFrame
{
    // The clock frame of the window's centre, on the take's timeline.
    std::int64_t frame = 0;

    // Zero when unvoiced.
    double hertz = 0.0;

    // 0-1: one minus YIN's normalised difference at the period. A clean
    // periodic tone is near 1; noise is far below.
    double confidence = 0.0;

    double rms = 0.0;

    [[nodiscard]] bool voiced() const noexcept
    {
        return hertz > 0.0;
    }
};

struct PitchContour
{
    // In time order, `hop` frames apart.
    std::vector<ContourFrame> frames;
    std::int64_t hop = 0;

    // The frames whose centres lie in [from, to), as indices.
    [[nodiscard]] std::pair<std::size_t, std::size_t>
    between(std::int64_t from, std::int64_t to) const noexcept
    {
        if (frames.empty() || hop <= 0 || to <= from)
        {
            return {0, 0};
        }
        const std::int64_t first = frames.front().frame;
        const auto index = [&](std::int64_t frame) -> std::size_t
        {
            if (frame <= first)
            {
                return 0;
            }
            const std::int64_t steps = ((frame - first) + hop - 1) / hop;
            return std::min(frames.size(), static_cast<std::size_t>(steps));
        };
        return {index(from), index(to)};
    }
};

// The window, in samples, at `sampleRate`.
[[nodiscard]] std::size_t contourWindowSize(double sampleRate, const ContourSettings& settings);

// One frame per hop over `audio`, whose first sample is clock frame
// `startFrame`. Only windows wholly inside the audio are analysed.
[[nodiscard]] PitchContour pitchContourOf(
    const float* audio,
    std::size_t count,
    double sampleRate,
    std::int64_t startFrame,
    const ContourSettings& settings = {});
} // namespace evaluation
