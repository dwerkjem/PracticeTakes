#pragma once

#include "ThemeType.h"

namespace AppDefaults
{
inline constexpr int schemaVersion = 1;
inline constexpr Theme theme = Theme::light;

namespace Audio
{
inline constexpr double inputGain = 1.0;
} // namespace Audio

namespace Tuner
{
inline constexpr int displayMode = 1;
inline constexpr double easing = 0.35;
inline constexpr double averaging = 5.0;
inline constexpr double noteSwitchSemitones = 0.55;
inline constexpr double dropoutFrames = 4.0;
inline constexpr double graphDurationSeconds = 20.0;
} // namespace Tuner

enum class Preset
{
    voice = 1,
    generalInstrument
};

struct TunerSettings
{
    double easing;
    double averaging;
    double noteSwitchSemitones;
    double dropoutFrames;
    double graphDurationSeconds;
};

[[nodiscard]] constexpr TunerSettings tunerPreset(Preset preset)
{
    if (preset == Preset::voice)
    {
        return {0.25, 7.0, 0.45, 7.0, 30.0};
    }

    return {
        Tuner::easing, Tuner::averaging, Tuner::noteSwitchSemitones, Tuner::dropoutFrames,
        Tuner::graphDurationSeconds};
}

[[nodiscard]] constexpr TunerSettings tunerDefaults()
{
    return tunerPreset(Preset::generalInstrument);
}
} // namespace AppDefaults
