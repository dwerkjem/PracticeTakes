#pragma once

#include "../../platform/midi/MidiMessageTranslation.h"
#include "../theme/ThemeType.h"

namespace AppDefaults
{
inline constexpr int schemaVersion = 7;
inline constexpr Theme theme = Theme::light;

namespace Audio
{
inline constexpr double inputGain = 1.0;
} // namespace Audio

namespace Midi
{
// The one default lives with the MIDI code, which is what applies it. Re-stated
// here so the settings layer reads every default from AppDefaults, but not
// re-declared: two 64s would drift the first time one changed.
inline constexpr int pedalDownThreshold = ::midi::defaultPedalDownThreshold;
} // namespace Midi

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
    int displayMode;
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
        return {Tuner::displayMode, 0.25, 7.0, 0.45, 7.0, 30.0};
    }

    return {Tuner::displayMode,         Tuner::easing,        Tuner::averaging,
            Tuner::noteSwitchSemitones, Tuner::dropoutFrames, Tuner::graphDurationSeconds};
}

[[nodiscard]] constexpr TunerSettings tunerDefaults()
{
    return tunerPreset(Preset::generalInstrument);
}
} // namespace AppDefaults
