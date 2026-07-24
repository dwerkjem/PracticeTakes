#pragma once

#include "AppDefaults.h"
#include "ThemeType.h"

#include <juce_data_structures/juce_data_structures.h>

namespace AppSettings
{
enum class RecentTool
{
    tuner = 1,
    spectrogram
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
    juce::String settingsBounds;
    RecentTool recentTool = RecentTool::tuner;
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
