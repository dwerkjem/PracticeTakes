#include "SettingsPersistence.h"

#include <cmath>

namespace
{
constexpr auto schemaKey = "settings.schema";
constexpr auto themeKey = "global.theme";
constexpr auto microphoneMutedKey = "audio.muted";
constexpr auto audioStateKey = "audio.deviceState";
constexpr auto audioInputGainKey = "audio.inputGain";
constexpr auto tunerDisplayModeKey = "tuner.displayMode";
constexpr auto tunerEasingKey = "tuner.easing";
constexpr auto tunerAveragingKey = "tuner.averaging";
constexpr auto tunerThresholdKey = "tuner.noteSwitch";
constexpr auto tunerDropoutKey = "tuner.dropout";
constexpr auto tunerDurationKey = "tuner.graphDuration";
constexpr auto tunerBoundsKey = "layout.tuner";
constexpr auto spectrogramBoundsKey = "layout.spectrogram";
constexpr auto settingsBoundsKey = "layout.settings";
constexpr auto recentToolKey = "layout.recentTool";

constexpr const char* ownedKeys[] = {
    schemaKey,           themeKey,       microphoneMutedKey,   audioStateKey,     audioInputGainKey,
    tunerDisplayModeKey, tunerEasingKey, tunerAveragingKey,    tunerThresholdKey, tunerDropoutKey,
    tunerDurationKey,    tunerBoundsKey, spectrogramBoundsKey, settingsBoundsKey, recentToolKey};

[[nodiscard]] bool hasOwnedSettings(const juce::PropertySet& properties)
{
    for (const auto* key : ownedKeys)
    {
        if (properties.containsKey(key))
        {
            return true;
        }
    }

    return false;
}

[[nodiscard]] double boundedValue(
    const juce::PropertySet& properties,
    const char* key,
    double fallback,
    double minimum,
    double maximum)
{
    const auto value = properties.getDoubleValue(key, fallback);
    return std::isfinite(value) && value >= minimum && value <= maximum ? value : fallback;
}

[[nodiscard]] Theme loadTheme(const juce::PropertySet& properties)
{
    const auto value = properties.getIntValue(themeKey, static_cast<int>(AppDefaults::theme));
    return value == static_cast<int>(Theme::dark) ? Theme::dark : Theme::light;
}

[[nodiscard]] AppSettings::RecentTool loadRecentTool(const juce::PropertySet& properties)
{
    const auto value =
        properties.getIntValue(recentToolKey, static_cast<int>(AppSettings::RecentTool::tuner));
    return value == static_cast<int>(AppSettings::RecentTool::spectrogram)
               ? AppSettings::RecentTool::spectrogram
               : AppSettings::RecentTool::tuner;
}

[[nodiscard]] int loadDisplayMode(const juce::PropertySet& properties)
{
    const auto value = properties.getIntValue(tunerDisplayModeKey, AppDefaults::Tuner::displayMode);
    return value >= 1 && value <= 3 ? value : AppDefaults::Tuner::displayMode;
}
} // namespace

AppSettings::LoadResult AppSettings::load(const juce::PropertiesFile& properties)
{
    LoadResult result;

    if (!properties.isValidFile())
    {
        result.status = LoadStatus::recoveredFromCorruption;
        return result;
    }

    const auto storedSchema = properties.getIntValue(schemaKey, 0);
    if (storedSchema > AppDefaults::schemaVersion)
    {
        result.status = LoadStatus::newerSchema;
        return result;
    }

    if (storedSchema < 0)
    {
        result.status = LoadStatus::recoveredFromCorruption;
        return result;
    }

    if (storedSchema == 0 && !hasOwnedSettings(properties))
    {
        result.status = LoadStatus::defaults;
        return result;
    }

    result.status =
        storedSchema == AppDefaults::schemaVersion ? LoadStatus::loaded : LoadStatus::migrated;
    result.state.theme = loadTheme(properties);
    result.state.microphoneMuted = properties.getBoolValue(microphoneMutedKey, false);
    result.state.inputGain =
        boundedValue(properties, audioInputGainKey, AppDefaults::Audio::inputGain, 0.0, 2.0);
    result.state.audioDeviceState = properties.getValue(audioStateKey);
    result.state.tuner = {
        loadDisplayMode(properties),
        boundedValue(properties, tunerEasingKey, AppDefaults::Tuner::easing, 0.02, 1.0),
        boundedValue(properties, tunerAveragingKey, AppDefaults::Tuner::averaging, 1.0, 15.0),
        boundedValue(
            properties, tunerThresholdKey, AppDefaults::Tuner::noteSwitchSemitones, 0.1, 1.5),
        boundedValue(properties, tunerDropoutKey, AppDefaults::Tuner::dropoutFrames, 1.0, 20.0),
        boundedValue(
            properties, tunerDurationKey, AppDefaults::Tuner::graphDurationSeconds, 5.0, 60.0)};
    result.state.tunerBounds = properties.getValue(tunerBoundsKey);
    result.state.spectrogramBounds = properties.getValue(spectrogramBoundsKey);
    result.state.settingsBounds = properties.getValue(settingsBoundsKey);
    result.state.recentTool = loadRecentTool(properties);
    return result;
}

void AppSettings::store(juce::PropertySet& properties, const State& state)
{
    properties.setValue(schemaKey, AppDefaults::schemaVersion);
    properties.setValue(themeKey, static_cast<int>(state.theme));
    properties.setValue(microphoneMutedKey, state.microphoneMuted);
    properties.setValue(audioInputGainKey, state.inputGain);
    properties.setValue(audioStateKey, state.audioDeviceState);
    properties.setValue(tunerDisplayModeKey, state.tuner.displayMode);
    properties.setValue(tunerEasingKey, state.tuner.easing);
    properties.setValue(tunerAveragingKey, state.tuner.averaging);
    properties.setValue(tunerThresholdKey, state.tuner.noteSwitchSemitones);
    properties.setValue(tunerDropoutKey, state.tuner.dropoutFrames);
    properties.setValue(tunerDurationKey, state.tuner.graphDurationSeconds);
    properties.setValue(tunerBoundsKey, state.tunerBounds);
    properties.setValue(spectrogramBoundsKey, state.spectrogramBounds);
    properties.setValue(settingsBoundsKey, state.settingsBounds);
    properties.setValue(recentToolKey, static_cast<int>(state.recentTool));
}

void AppSettings::clearOwnedValues(juce::PropertySet& properties)
{
    for (const auto* key : ownedKeys)
    {
        properties.removeValue(key);
    }
}
