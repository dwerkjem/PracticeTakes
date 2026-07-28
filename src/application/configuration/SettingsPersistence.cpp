#include "SettingsPersistence.h"

#include "../shell/ui/workspace/model/WorkspaceCatalogCodec.h"

#include <array>
#include <charconv>
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
constexpr auto workspaceCatalogKey = "workspace.catalog";
constexpr auto tunerBoundsKey = "workspace.lastFloating.tuner";
constexpr auto spectrogramBoundsKey = "workspace.lastFloating.spectrogram";
constexpr auto harmonicBoundsKey = "workspace.lastFloating.harmonicAnalyzer";
constexpr auto settingsBoundsKey = "window.settingsBounds";
constexpr auto fullscreenModeKey = "window.fullscreenMode";
constexpr auto legacyTunerBoundsKey = "layout.tuner";
constexpr auto legacySpectrogramBoundsKey = "layout.spectrogram";
constexpr auto legacyHarmonicBoundsKey = "layout.harmonics";
constexpr auto legacySettingsBoundsKey = "layout.settings";
constexpr auto legacyRecentToolKey = "layout.recentTool";

constexpr int minimumWindowSize = 300;
constexpr int maximumWindowSize = 10000;

constexpr const char* legacyLayoutKeys[] = {
    legacyTunerBoundsKey, legacySpectrogramBoundsKey, legacyHarmonicBoundsKey,
    legacySettingsBoundsKey, legacyRecentToolKey};

constexpr const char* ownedKeys[] = {
    schemaKey,
    themeKey,
    microphoneMutedKey,
    audioStateKey,
    audioInputGainKey,
    tunerDisplayModeKey,
    tunerEasingKey,
    tunerAveragingKey,
    tunerThresholdKey,
    tunerDropoutKey,
    tunerDurationKey,
    workspaceCatalogKey,
    tunerBoundsKey,
    spectrogramBoundsKey,
    harmonicBoundsKey,
    settingsBoundsKey,
    fullscreenModeKey,
    legacyTunerBoundsKey,
    legacySpectrogramBoundsKey,
    legacyHarmonicBoundsKey,
    legacySettingsBoundsKey,
    legacyRecentToolKey};

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

[[nodiscard]] juce::String
loadValidWindowBounds(const juce::PropertySet& properties, const char* key)
{
    const auto stored = properties.getValue(key);
    juce::StringArray tokens;
    tokens.addTokens(stored, ",; \t\r\n", {});
    if (tokens.size() != 4)
    {
        return {};
    }

    std::array<int, 4> values{};
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        const auto token = tokens[static_cast<int>(index)].toStdString();
        const auto [end, error] =
            std::from_chars(token.data(), token.data() + token.size(), values[index]);
        if (error != std::errc{} || end != token.data() + token.size())
        {
            return {};
        }
    }

    if (values[2] < minimumWindowSize || values[3] < minimumWindowSize ||
        values[2] > maximumWindowSize || values[3] > maximumWindowSize)
    {
        return {};
    }
    return juce::String(values[0]) + " " + juce::String(values[1]) + " " + juce::String(values[2]) +
           " " + juce::String(values[3]);
}

[[nodiscard]] AppSettings::FullscreenMode loadFullscreenMode(const juce::PropertySet& properties)
{
    const auto value = properties.getIntValue(
        fullscreenModeKey, static_cast<int>(AppSettings::FullscreenMode::normal));
    return value == static_cast<int>(AppSettings::FullscreenMode::kiosk)
               ? AppSettings::FullscreenMode::kiosk
               : AppSettings::FullscreenMode::normal;
}

[[nodiscard]] int loadDisplayMode(const juce::PropertySet& properties)
{
    const auto value = properties.getIntValue(tunerDisplayModeKey, AppDefaults::Tuner::displayMode);
    return value >= 1 && value <= 3 ? value : AppDefaults::Tuner::displayMode;
}

[[nodiscard]] ToolSettingsPayload tunerPayload(const AppDefaults::TunerSettings& settings)
{
    auto* object = new juce::DynamicObject;
    object->setProperty("displayMode", settings.displayMode);
    object->setProperty("easing", settings.easing);
    object->setProperty("averaging", settings.averaging);
    object->setProperty("noteSwitchSemitones", settings.noteSwitchSemitones);
    object->setProperty("dropoutFrames", settings.dropoutFrames);
    object->setProperty("graphDurationSeconds", settings.graphDurationSeconds);
    return {1, juce::JSON::toString(juce::var(object)).toStdString()};
}

[[nodiscard]] WorkspaceCatalog
migratedWorkspaceCatalog(const AppDefaults::TunerSettings& tunerSettings)
{
    WorkspaceCatalog catalog;
    catalog.active = WorkspaceBuiltIns::pitchPractice().snapshot;
    catalog.active.toolSettings.insert_or_assign("tuner", tunerPayload(tunerSettings));
    catalog.activeSource = WorkspaceBuiltIns::pitchPractice().id;
    return catalog;
}

[[nodiscard]] AppSettings::RecentTool recentTool(const WorkspaceCatalog& catalog)
{
    if (catalog.active.focusedTool == "spectrogram")
    {
        return AppSettings::RecentTool::spectrogram;
    }
    if (catalog.active.focusedTool == "harmonic-analyzer")
    {
        return AppSettings::RecentTool::harmonics;
    }
    return AppSettings::RecentTool::tuner;
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

    const auto isCurrentSchema = storedSchema == AppDefaults::schemaVersion;
    result.status = isCurrentSchema ? LoadStatus::loaded : LoadStatus::migrated;
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
    result.state.fullscreenMode = loadFullscreenMode(properties);

    if (!isCurrentSchema)
    {
        result.state.tunerBounds = loadValidWindowBounds(properties, legacyTunerBoundsKey);
        result.state.spectrogramBounds =
            loadValidWindowBounds(properties, legacySpectrogramBoundsKey);
        result.state.harmonicBounds = loadValidWindowBounds(properties, legacyHarmonicBoundsKey);
        result.state.settingsBounds = loadValidWindowBounds(properties, legacySettingsBoundsKey);
        result.state.workspaceCatalog = migratedWorkspaceCatalog(result.state.tuner);
        result.state.recentTool = recentTool(result.state.workspaceCatalog);
        return result;
    }

    result.state.tunerBounds = loadValidWindowBounds(properties, tunerBoundsKey);
    result.state.spectrogramBounds = loadValidWindowBounds(properties, spectrogramBoundsKey);
    result.state.harmonicBounds = loadValidWindowBounds(properties, harmonicBoundsKey);
    result.state.settingsBounds = loadValidWindowBounds(properties, settingsBoundsKey);

    const auto document = properties.getValue(workspaceCatalogKey).toStdString();
    const auto decoded = WorkspaceCatalogCodec::decode(document, {});
    if (decoded.status == WorkspaceCatalogDecodeStatus::newerSchema)
    {
        result.status = LoadStatus::newerSchema;
        return result;
    }
    if (!decoded.catalog.has_value())
    {
        result.status = LoadStatus::recoveredFromCorruption;
        return result;
    }

    result.state.workspaceCatalog = *decoded.catalog;
    result.state.recentTool = recentTool(result.state.workspaceCatalog);
    if (decoded.status == WorkspaceCatalogDecodeStatus::loadedWithRecovery)
    {
        result.status = LoadStatus::recoveredFromCorruption;
    }
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
    const auto catalog = WorkspaceCatalogCodec::encode(state.workspaceCatalog);
    properties.setValue(
        workspaceCatalogKey,
        juce::String::fromUTF8(catalog.data(), static_cast<int>(catalog.size())));
    properties.setValue(tunerBoundsKey, state.tunerBounds);
    properties.setValue(spectrogramBoundsKey, state.spectrogramBounds);
    properties.setValue(harmonicBoundsKey, state.harmonicBounds);
    properties.setValue(settingsBoundsKey, state.settingsBounds);
    properties.setValue(fullscreenModeKey, static_cast<int>(state.fullscreenMode));
    for (const auto* key : legacyLayoutKeys)
    {
        properties.removeValue(key);
    }
}

void AppSettings::clearOwnedValues(juce::PropertySet& properties)
{
    for (const auto* key : ownedKeys)
    {
        properties.removeValue(key);
    }
}
