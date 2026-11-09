#include "SettingsPersistence.h"

#include "../../features/analysis/tuner/TunerSettingsCodec.h"
#include "../shell/ui/workspace/model/WorkspaceCatalogCodec.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>

namespace
{
constexpr auto schemaKey = "settings.schema";
constexpr auto themeKey = "global.theme";
constexpr auto textScaleKey = "appearance.textScale";
constexpr auto noteNamingKey = "appearance.noteNaming";
constexpr auto languageKey = "appearance.language";
constexpr auto octaveNumberingKey = "appearance.octaveNumbering";
constexpr auto microphoneMutedKey = "audio.muted";
constexpr auto audioStateKey = "audio.deviceState";
constexpr auto audioInputGainKey = "audio.inputGain";
constexpr auto midiInputDeviceIdentifierKey = "midi.inputDeviceIdentifier";
constexpr auto midiInputDeviceNameKey = "midi.inputDeviceName";
constexpr auto midiPedalDownThresholdKey = "midi.pedalDownThreshold";
constexpr auto metronomeEnabledKey = "metronome.enabled";
constexpr auto metronomeVolumeKey = "metronome.volume";
constexpr auto metronomeAccentKey = "metronome.accent";
constexpr auto countInBarsKey = "practice.countInBars";
constexpr auto playbackEnabledKey = "playback.enabled";
constexpr auto playbackVolumeKey = "playback.volume";
constexpr auto referencePitchKey = "practice.referencePitchHz";
constexpr auto inTuneCentsKey = "practice.inTuneCents";
constexpr auto nearPerfectCentsKey = "practice.nearPerfectCents";
constexpr auto outOfTuneCentsKey = "practice.outOfTuneCents";
constexpr auto minimumLevelKey = "practice.minimumLevelDb";
constexpr auto countInWhenPlayingKey = "practice.countInWhenRecordingDuringPlayback";
constexpr auto chosenTargetSoundingKey = "practice.chosenTarget";
constexpr auto chosenTargetWrittenKey = "practice.chosenTargetWritten";
constexpr auto keepTargetKey = "practice.keepTargetWhenScoreOpens";
constexpr auto targetFollowsKey = "practice.targetFollowsScore";
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
    textScaleKey,
    noteNamingKey,
    octaveNumberingKey,
    languageKey,
    microphoneMutedKey,
    audioStateKey,
    audioInputGainKey,
    midiInputDeviceIdentifierKey,
    midiInputDeviceNameKey,
    midiPedalDownThresholdKey,
    metronomeEnabledKey,
    metronomeVolumeKey,
    metronomeAccentKey,
    countInBarsKey,
    playbackEnabledKey,
    playbackVolumeKey,
    referencePitchKey,
    inTuneCentsKey,
    nearPerfectCentsKey,
    outOfTuneCentsKey,
    minimumLevelKey,
    countInWhenPlayingKey,
    chosenTargetSoundingKey,
    chosenTargetWrittenKey,
    keepTargetKey,
    targetFollowsKey,
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
    return value >= AppDefaults::tunerDisplayModeMinimum &&
                   value <= AppDefaults::tunerDisplayModeMaximum
               ? value
               : AppDefaults::Tuner::displayMode;
}

[[nodiscard]] WorkspaceCatalog
migratedWorkspaceCatalog(const AppDefaults::TunerSettings& tunerSettings)
{
    WorkspaceCatalog catalog;
    catalog.active = WorkspaceBuiltIns::pitchPractice().snapshot;
    catalog.active.toolSettings.insert_or_assign(
        "tuner", TunerSettingsCodec::encode(tunerSettings));
    catalog.activeSource = WorkspaceBuiltIns::pitchPractice().id;
    return catalog;
}

// A number out of range is clamped rather than replaced by the default: a
// hand-edited 200 means "as far as it goes", and a 0 would mean the pedal is
// always down. A value that is not a number at all is not a preference, so it
// gets the default -- `getIntValue` would read it as 0 and clamp it to 1, which
// is the opposite of a sensible fallback.
//
// There is deliberately no schema bump for this key. It is purely additive (a
// file without it takes the default), and a bump sends every older file down the
// pre-workspace migration path in `load`, which discards saved workspaces.
[[nodiscard]] int loadPedalDownThreshold(const juce::PropertySet& properties)
{
    const auto text = properties.getValue(midiPedalDownThresholdKey).trim();

    if (text.isEmpty() || !text.containsOnly("-0123456789") || !text.containsAnyOf("0123456789"))
    {
        return AppDefaults::Midi::pedalDownThreshold;
    }

    return std::clamp(
        text.getIntValue(), ::midi::minimumPedalDownThreshold, ::midi::maximumPedalDownThreshold);
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
    // Missing (a file from before the setting), out of range or not a number:
    // the nearest size the setting offers, or 100.
    result.state.textScalePercent = appearance::TextScale::snapped(properties.getDoubleValue(
        textScaleKey, static_cast<double>(appearance::TextScale::defaultPercent)));
    // Unknown values -- a file from a later version with a convention this one
    // lacks -- fall back to the default rather than to a neighbour.
    const auto convention = [&properties](const char* key)
    {
        const int stored = properties.getIntValue(key, 0);
        return stored >= 0 && stored <= 2 ? stored : 0;
    };
    result.state.noteNaming = convention(noteNamingKey);
    result.state.octaveNumbering = convention(octaveNumberingKey);
    result.state.microphoneMuted = properties.getBoolValue(microphoneMutedKey, false);
    result.state.inputGain =
        boundedValue(properties, audioInputGainKey, AppDefaults::Audio::inputGain, 0.0, 2.0);
    result.state.audioDeviceState = properties.getValue(audioStateKey);
    result.state.midiInputDeviceIdentifier = properties.getValue(midiInputDeviceIdentifierKey);
    result.state.midiInputDeviceName = properties.getValue(midiInputDeviceNameKey);
    result.state.midiPedalDownThreshold = loadPedalDownThreshold(properties);
    // Additive keys with no schema bump, for the reason loadPedalDownThreshold
    // gives: a file without them takes the defaults.
    result.state.metronomeEnabled =
        properties.getBoolValue(metronomeEnabledKey, AppDefaults::Metronome::enabled);
    result.state.metronomeVolume =
        boundedValue(properties, metronomeVolumeKey, AppDefaults::Metronome::volume, 0.0, 1.0);
    result.state.metronomeAccent =
        properties.getBoolValue(metronomeAccentKey, AppDefaults::Metronome::accent);
    result.state.countInBars = static_cast<int>(
        boundedValue(properties, countInBarsKey, AppDefaults::Metronome::countInBars, 0.0, 4.0));
    result.state.playbackEnabled =
        properties.getBoolValue(playbackEnabledKey, AppDefaults::Playback::enabled);
    result.state.playbackVolume =
        boundedValue(properties, playbackVolumeKey, AppDefaults::Playback::volume, 0.0, 1.0);
    result.state.referencePitchHz =
        boundedValue(properties, referencePitchKey, AppDefaults::referencePitchHz, 415.0, 466.0);
    {
        // Each read in its own range, then put in order: a file written by
        // hand, or by 0.6.0 with neither the near-perfect nor the out-of-tune
        // key, can disagree with itself.
        const auto limits = pitch::orderedLimits(
            {boundedValue(
                 properties, nearPerfectCentsKey, AppDefaults::nearPerfectCents,
                 AppDefaults::minimumNearPerfectCents, AppDefaults::maximumNearPerfectCents),
             boundedValue(
                 properties, inTuneCentsKey, AppDefaults::inTuneCents,
                 AppDefaults::minimumInTuneCents, AppDefaults::maximumInTuneCents),
             boundedValue(
                 properties, outOfTuneCentsKey, AppDefaults::outOfTuneCents,
                 AppDefaults::minimumOutOfTuneCents, AppDefaults::maximumOutOfTuneCents)});
        result.state.inTuneCents = limits.toleranceCents;
        result.state.nearPerfectCents = limits.nearPerfectCents;
        result.state.outOfTuneCents = limits.outOfTuneCents;
    }
    result.state.minimumLevelDb = boundedValue(
        properties, minimumLevelKey, AppDefaults::minimumLevelDb, AppDefaults::lowestMinimumLevelDb,
        AppDefaults::highestMinimumLevelDb);
    result.state.countInWhenRecordingDuringPlayback =
        properties.getBoolValue(countInWhenPlayingKey, false);
    result.state.language = storedLanguage(properties);
    result.state.chosenTargetSounding = properties.getValue(chosenTargetSoundingKey);
    result.state.chosenTargetWritten = properties.getValue(chosenTargetWrittenKey);
    result.state.keepTargetWhenScoreOpens = properties.getBoolValue(keepTargetKey, false);
    result.state.targetFollowsScore = properties.getBoolValue(targetFollowsKey, true);
    // The pre-workspace keys, read against the ranges in the tuner's schema.
    const auto tunerValue =
        [&properties](const char* key, double AppDefaults::TunerSettings::*member)
    {
        const AppDefaults::TunerNumberField& field = AppDefaults::tunerField(member);
        return boundedValue(
            properties, key, AppDefaults::tunerDefaults().*member, field.minimum, field.maximum);
    };
    result.state.tuner = {
        loadDisplayMode(properties),
        tunerValue(tunerEasingKey, &AppDefaults::TunerSettings::easing),
        tunerValue(tunerAveragingKey, &AppDefaults::TunerSettings::averaging),
        tunerValue(tunerThresholdKey, &AppDefaults::TunerSettings::noteSwitchSemitones),
        tunerValue(tunerDropoutKey, &AppDefaults::TunerSettings::dropoutFrames),
        tunerValue(tunerDurationKey, &AppDefaults::TunerSettings::graphDurationSeconds),
        // No pre-workspace key: these files predate the settings.
        AppDefaults::Tuner::scaleCents, AppDefaults::Tuner::signs,
        AppDefaults::Tuner::bothHalvesLit, AppDefaults::Tuner::marker,
        AppDefaults::Tuner::offsetShown, AppDefaults::Tuner::targetCentred};
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

juce::String AppSettings::storedLanguage(const juce::PropertySet& properties)
{
    // A code is letters, digits, '-' and '_'; anything else is not one.
    const auto stored = properties.getValue(languageKey).trim();
    return stored.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")
               ? stored
               : juce::String();
}

void AppSettings::store(juce::PropertySet& properties, const State& state)
{
    properties.setValue(schemaKey, AppDefaults::schemaVersion);
    properties.setValue(themeKey, static_cast<int>(state.theme));
    properties.setValue(textScaleKey, state.textScalePercent);
    properties.setValue(noteNamingKey, state.noteNaming);
    properties.setValue(octaveNumberingKey, state.octaveNumbering);
    properties.setValue(microphoneMutedKey, state.microphoneMuted);
    properties.setValue(audioInputGainKey, state.inputGain);
    properties.setValue(audioStateKey, state.audioDeviceState);
    properties.setValue(midiInputDeviceIdentifierKey, state.midiInputDeviceIdentifier);
    properties.setValue(midiInputDeviceNameKey, state.midiInputDeviceName);
    properties.setValue(midiPedalDownThresholdKey, state.midiPedalDownThreshold);
    properties.setValue(metronomeEnabledKey, state.metronomeEnabled);
    properties.setValue(metronomeVolumeKey, state.metronomeVolume);
    properties.setValue(metronomeAccentKey, state.metronomeAccent);
    properties.setValue(countInBarsKey, state.countInBars);
    properties.setValue(playbackEnabledKey, state.playbackEnabled);
    properties.setValue(playbackVolumeKey, state.playbackVolume);
    properties.setValue(referencePitchKey, state.referencePitchHz);
    properties.setValue(inTuneCentsKey, state.inTuneCents);
    properties.setValue(nearPerfectCentsKey, state.nearPerfectCents);
    properties.setValue(outOfTuneCentsKey, state.outOfTuneCents);
    properties.setValue(minimumLevelKey, state.minimumLevelDb);
    properties.setValue(countInWhenPlayingKey, state.countInWhenRecordingDuringPlayback);
    properties.setValue(languageKey, state.language);
    properties.setValue(chosenTargetSoundingKey, state.chosenTargetSounding);
    properties.setValue(chosenTargetWrittenKey, state.chosenTargetWritten);
    properties.setValue(keepTargetKey, state.keepTargetWhenScoreOpens);
    properties.setValue(targetFollowsKey, state.targetFollowsScore);
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
