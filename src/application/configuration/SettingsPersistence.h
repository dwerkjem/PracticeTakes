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
