#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "PerformedOnset.h"

// Where attacks begin in a take's audio, by spectral flux (no JUCE
// dependency). openspec/changes/archive/2026-11-08-onset-timing/design.md, decision 3.
namespace evaluation
{
struct OnsetSettings
{
    // The analysis window and the step between windows, in samples. Powers of
    // two; the window is a Hann.
    std::size_t windowSize = 1024;
    std::size_t hop = 128;

    // The flux of a log-compressed spectrum rises as soon as an attack enters
    // the window, so its peak comes this fraction of a window early. Measured
    // on plucked tones from 110 Hz to 1.76 kHz, fast and slow decays alike, it
    // varies by under 10 samples; it is added back.
    double peakLeadFraction = 0.277;

    // A peak counts when the flux rises above `medianScale` times the median
    // around it plus `peakFraction` of the loudest flux in the take. Relative
    // rather than absolute, so the same settings hold for a quiet voice and a
    // loud piano; multiplicative on the median, so steady noise -- whose flux
    // barely varies -- finds nothing.
    double medianScale = 1.5;
    double peakFraction = 0.05;
    std::size_t medianRadius = 10;

    // Two onsets are at least this far apart: the faster is ornament, which
    // timing does not judge.
    double minimumGapSeconds = 0.05;

    // Sound quieter than this, in dB below full scale, is never an attack:
    // the room, breathing, a chair. The thresholds above are relative to the
    // take, so in a quiet stretch -- or a take of nothing but the room -- the
    // room's own small peaks cleared them and came back as extra notes. This
    // is the absolute floor under them: the level of the window just after a
    // peak must reach it. The singer's setting (Settings -> Practice, "Ignore
    // sound below").
    double minimumLevelDb = defaultMinimumLevelDb;

    static constexpr double defaultMinimumLevelDb = -45.0;
    static constexpr double lowestMinimumLevelDb = -80.0;
    static constexpr double highestMinimumLevelDb = -20.0;
};

// The level of `count` samples from `first`, in dB below full scale: the RMS,
// or minus infinity for silence.
[[nodiscard]] double levelDb(const float* audio, std::size_t first, std::size_t count) noexcept;

// Every onset in `audio`, whose first sample is clock frame `startFrame`, in
// time order. Each frame is refined between hops by fitting a parabola to the
// flux peak. Only windows wholly inside the audio are judged: where the audio
// is cut off -- the end of a take, mid-note -- is not an attack.
[[nodiscard]] std::vector<PerformedOnset> detectOnsets(
    const float* audio,
    std::size_t count,
    double sampleRate,
    std::int64_t startFrame,
    const OnsetSettings& settings = {});
} // namespace evaluation
