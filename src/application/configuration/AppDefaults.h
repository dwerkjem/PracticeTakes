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

// The practice clock's metronome and count-in. Preferences about this
// machine's speakers and this player's habits, so -- like the MIDI keyboard --
// saved locally and never carried in a settings transfer file.
namespace Metronome
{
inline constexpr bool enabled = true;
inline constexpr double volume = 0.7;
inline constexpr bool accent = true;
inline constexpr int countInBars = 1;
} // namespace Metronome

// Playing the score through the clock (openspec/changes/archive/2026-11-08-score-playback): off
// until someone turns it on, so the clock sounds as it always did. Local, like
// the metronome.
// The application's A4 in hertz (system-wide-target-note). Local, like the
// metronome: a preference about where this person practises.
inline constexpr double referencePitchHz = 440.0;

// How close a sung note must be to be in tune, in cents either way
// (openspec/changes/archive/2026-11-09-sung-pitch-judging, decision 4). Local, like the reference.
inline constexpr double inTuneCents = 25.0;
inline constexpr double minimumInTuneCents = 5.0;
inline constexpr double maximumInTuneCents = 50.0;

namespace Playback
{
inline constexpr bool enabled = false;
inline constexpr double volume = 0.8;
} // namespace Playback

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

// The tuner's settings schema: for each numeric setting, the name it is stored
// under in a workspace payload and the range every reader accepts and the
// tuner's slider offers. Declared once because three places read it -- the
// workspace codec, the legacy settings migration and the tuner's own controls
// -- and each used to spell the ranges out for itself.
struct TunerNumberField
{
    std::string_view key;
    double TunerSettings::*member;
    double minimum;
    double maximum;
    double step;
};

inline constexpr int tunerDisplayModeMinimum = 1;
inline constexpr int tunerDisplayModeMaximum = 3;

// In payload order: a workspace payload lists its fields in this order.
inline constexpr std::array tunerNumberFields{
    TunerNumberField{"easing", &TunerSettings::easing, 0.02, 1.0, 0.01},
    TunerNumberField{"averaging", &TunerSettings::averaging, 1.0, 15.0, 1.0},
    TunerNumberField{"noteSwitchSemitones", &TunerSettings::noteSwitchSemitones, 0.1, 1.5, 0.05},
    TunerNumberField{"dropoutFrames", &TunerSettings::dropoutFrames, 1.0, 20.0, 1.0},
    TunerNumberField{"graphDurationSeconds", &TunerSettings::graphDurationSeconds, 5.0, 60.0, 1.0},
};

// The schema entry for one setting. Every member of TunerSettings but
// displayMode has one, which AppDefaultsTests checks.
[[nodiscard]] constexpr const TunerNumberField& tunerField(double TunerSettings::*member)
{
    for (const TunerNumberField& field : tunerNumberFields)
    {
        if (field.member == member)
        {
            return field;
        }
    }
    return tunerNumberFields.front();
}

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
