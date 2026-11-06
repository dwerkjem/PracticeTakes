#pragma once

#include "../../platform/midi/MidiMessageTranslation.h"
#include "../theme/ThemeType.h"

#include <array>
#include <string_view>

namespace AppDefaults
{
inline constexpr int schemaVersion = 6;
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

// The general-instrument values, which are also the tuner's defaults.
inline constexpr TunerSettings generalInstrumentTuner{
    Tuner::displayMode,         Tuner::easing,        Tuner::averaging,
    Tuner::noteSwitchSemitones, Tuner::dropoutFrames, Tuner::graphDurationSeconds};

// A practice preset, declared once: its identity, the name the Settings window
// offers it under, and the tuner controls it applies. The Settings window
// builds its preset list from this table, so a new preset is one entry here
// and nothing in the shell -- the pattern `ToolDefinition` set for tools.
struct PresetDefinition
{
    Preset id;
    std::string_view label;
    TunerSettings tuner;
};

// In the order the Settings window lists them. An id is also the preset's
// ComboBox item id, which JUCE reserves 0 of, so ids start at 1.
inline constexpr std::array presets{
    PresetDefinition{
        Preset::voice,
        "Voice practice",
        {Tuner::displayMode, 0.25, 7.0, 0.45, 7.0, 30.0}},
    PresetDefinition{Preset::generalInstrument, "General instrument", generalInstrumentTuner},
};

[[nodiscard]] constexpr TunerSettings tunerPreset(Preset preset)
{
    for (const PresetDefinition& definition : presets)
    {
        if (definition.id == preset)
        {
            return definition.tuner;
        }
    }

    // An id from nowhere -- a ComboBox item this table did not add -- applies
    // the defaults, as the code before the table did for anything not voice.
    return generalInstrumentTuner;
}

[[nodiscard]] constexpr TunerSettings tunerDefaults()
{
    return generalInstrumentTuner;
}
} // namespace AppDefaults
