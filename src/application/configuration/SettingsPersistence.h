#pragma once

#include "../shell/ui/workspace/model/WorkspaceBuiltIns.h"
#include "../theme/ThemeType.h"
#include "AppDefaults.h"

#include <juce_data_structures/juce_data_structures.h>

namespace AppSettings
{
enum class RecentTool
{
    tuner = 1,
    spectrogram,
    harmonics
};

enum class FullscreenMode
{
    normal = 1,
    kiosk
};

enum class LoadStatus
{
    loaded,
    defaults,
    migrated,
    recoveredFromCorruption,
    newerSchema
};

struct State
{
    Theme theme = AppDefaults::theme;
    bool microphoneMuted = false;
    double inputGain = AppDefaults::Audio::inputGain;
    juce::String audioDeviceState;

    // The chosen MIDI input, by the identifier that reopens it and the name
    // that identifies it to a person. Both, because a keyboard that is not
    // plugged in still has to be nameable in the interface -- "Casio PX-S1100
    // (not connected)" rather than an opaque identifier or a blank.
    juce::String midiInputDeviceIdentifier;
    juce::String midiInputDeviceName;

    // Where the sustain pedal counts as down: at or above this, 1-127. A
    // preference about a particular keyboard's pedal, which is why -- like the
    // device selection -- it is not carried in a settings transfer file.
    int midiPedalDownThreshold = AppDefaults::Midi::pedalDownThreshold;

    // The practice clock's metronome, and how many bars it counts in. Local to
    // this machine and not carried in a transfer file, like the MIDI keyboard.
    bool metronomeEnabled = AppDefaults::Metronome::enabled;
    double metronomeVolume = AppDefaults::Metronome::volume;
    bool metronomeAccent = AppDefaults::Metronome::accent;
    int countInBars = AppDefaults::Metronome::countInBars;

    // Whether the score plays with the clock, and how loud. Local too.
    bool playbackEnabled = AppDefaults::Playback::enabled;
    double playbackVolume = AppDefaults::Playback::volume;

    // A4 in hertz, 415-466.
    double referencePitchHz = AppDefaults::referencePitchHz;

    // A target the player chose, as its sounding and written names ("Bb3"),
    // empty for none; and whether opening a score keeps it (target-note-picker).
    juce::String chosenTargetSounding;
    juce::String chosenTargetWritten;
    bool keepTargetWhenScoreOpens = false;
    AppDefaults::TunerSettings tuner = AppDefaults::tunerDefaults();
    juce::String tunerBounds;
    juce::String spectrogramBounds;
    juce::String harmonicBounds;
    juce::String settingsBounds;
    RecentTool recentTool = RecentTool::tuner;
    FullscreenMode fullscreenMode = FullscreenMode::normal;
    WorkspaceCatalog workspaceCatalog = []
    {
        WorkspaceCatalog catalog;
        catalog.active = WorkspaceBuiltIns::pitchPractice().snapshot;
        catalog.activeSource = WorkspaceBuiltIns::pitchPractice().id;
        return catalog;
    }();
};

struct LoadResult
{
    State state;
    LoadStatus status = LoadStatus::defaults;
};

[[nodiscard]] LoadResult load(const juce::PropertiesFile& properties);
void store(juce::PropertySet& properties, const State& state);
void clearOwnedValues(juce::PropertySet& properties);
} // namespace AppSettings
