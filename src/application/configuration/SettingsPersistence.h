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
