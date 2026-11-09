#pragma once

#include "../../platform/evaluation/OnsetDetector.h"
#include "../../platform/midi/MidiMessageTranslation.h"
#include "../../platform/pitch/IntonationZone.h"
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
// What a saved value may be and still be read. 50 is accepted because 0.6.0
// allowed it; loading then holds it below the near-note limit
// (pitch::orderedLimits), so it comes back as 49 rather than as the default.
inline constexpr double minimumInTuneCents = pitch::minimumToleranceCents;
inline constexpr double maximumInTuneCents = 50.0;

// How close a sung note must be to be near perfect, in cents either way: the
// innermost zone, always inside "in tune within". Local, like it.
inline constexpr double nearPerfectCents = pitch::nearPerfectCents;
inline constexpr double minimumNearPerfectCents = pitch::minimumNearPerfectCents;
inline constexpr double maximumNearPerfectCents = pitch::maximumNearPerfectCents;

// Past how many cents, either way, a sung note is out of tune: where the amber
// zone turns red, always outside "in tune within" and no wider than a note
// nearer the next one. Local, like it.
inline constexpr double outOfTuneCents = pitch::outOfTuneCents;
inline constexpr double minimumOutOfTuneCents = pitch::minimumOutOfTuneCents;
inline constexpr double maximumOutOfTuneCents = pitch::maximumOutOfTuneCents;

// The quietest sound a microphone take counts as playing or singing, in dB
// below full scale: below it the room is not an attack and never an extra
// note (#349). Local, like the tolerances: it depends on this microphone and
// this room.
inline constexpr double minimumLevelDb = evaluation::OnsetSettings::defaultMinimumLevelDb;
inline constexpr double lowestMinimumLevelDb = evaluation::OnsetSettings::lowestMinimumLevelDb;
inline constexpr double highestMinimumLevelDb = evaluation::OnsetSettings::highestMinimumLevelDb;

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

// How far either way the bar and the dial reach: one note, ±50 cents.
inline constexpr double scaleCents = pitch::nearNoteCents;

// What the lit half of a zone carries: 0 nothing, 1 a sharp or flat sign, 2
// an arrow the way to go (down when sharp, up when flat). And whether both
// halves light (1) rather than only the one the pitch is on (0). Numbers, so
// the one schema and codec cover them.
inline constexpr double signs = 1.0;
inline constexpr double bothHalvesLit = 0.0;

// What marks where the pitch is on the bar and the graph: 0 nothing, 1 a dot,
// 2 an arrow, 3 a line across, 4 a ring, 5 a diamond, 6 a crosshair.
inline constexpr double marker = 1.0;

// Whether the lit half also says how far off the pitch is, in cents (1), or
// not (0). Off by default: the status line already gives the exact reading.
inline constexpr double offsetShown = 0.0;

// Whether, with a target note chosen, the tuner centres on it (1) -- its zones,
// sign and marker measuring against the target, the graph's axis centred on
// its line -- or reads against the nearest note as it does without one (0).
inline constexpr double targetCentred = 1.0;
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
    double scaleCents;
    double signs;
    double bothHalvesLit;
    double marker;
    double offsetShown;
    double targetCentred;
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

    // Whether a payload must carry it. A field added after a release is not:
    // a workspace saved before it has no such key, and takes the default
    // rather than losing every other setting with it.
    bool required = true;
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
    // Added in 0.7.0.
    TunerNumberField{"scaleCents", &TunerSettings::scaleCents, 10.0, 100.0, 5.0, false},
    TunerNumberField{"signs", &TunerSettings::signs, 0.0, 2.0, 1.0, false},
    TunerNumberField{"bothHalvesLit", &TunerSettings::bothHalvesLit, 0.0, 1.0, 1.0, false},
    TunerNumberField{"marker", &TunerSettings::marker, 0.0, 6.0, 1.0, false},
    TunerNumberField{"offsetShown", &TunerSettings::offsetShown, 0.0, 1.0, 1.0, false},
    TunerNumberField{"targetCentred", &TunerSettings::targetCentred, 0.0, 1.0, 1.0, false},
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
    Tuner::displayMode,   Tuner::easing,
    Tuner::averaging,     Tuner::noteSwitchSemitones,
    Tuner::dropoutFrames, Tuner::graphDurationSeconds,
    Tuner::scaleCents,    Tuner::signs,
    Tuner::bothHalvesLit, Tuner::marker,
    Tuner::offsetShown,   Tuner::targetCentred};

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
        {Tuner::displayMode, 0.25, 7.0, 0.45, 7.0, 30.0, Tuner::scaleCents, Tuner::signs,
         Tuner::bothHalvesLit, Tuner::marker, Tuner::offsetShown, Tuner::targetCentred}},
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

// A preset applied to a tuner keeps the tuner's look -- its scale, signs,
// halves, marker and offset, chosen from its ... menu -- and changes only how
// it reads pitch. A preset is about the material, not about how the tuner is
// drawn.
[[nodiscard]] constexpr TunerSettings
presetKeepingLook(TunerSettings preset, const TunerSettings& current)
{
    preset.scaleCents = current.scaleCents;
    preset.signs = current.signs;
    preset.bothHalvesLit = current.bothHalvesLit;
    preset.marker = current.marker;
    preset.offsetShown = current.offsetShown;
    preset.targetCentred = current.targetCentred;
    return preset;
}

[[nodiscard]] constexpr TunerSettings tunerDefaults()
{
    return generalInstrumentTuner;
}
} // namespace AppDefaults
