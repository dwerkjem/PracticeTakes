#include "SettingsTransferCodec.h"

#include "../../platform/evaluation/OnsetTiming.h"
#include "../../platform/pitch/IntonationZone.h"
#include "../localisation/LocalisationRules.h"
#include "AudioDeviceStateTransfer.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <limits>
#include <memory>
#include <unordered_set>

namespace
{
[[nodiscard]] juce::String toJuceString(std::string_view value)
{
    return juce::String::fromUTF8(value.data(), static_cast<int>(value.size()));
}

[[nodiscard]] bool hasProperty(const juce::DynamicObject& object, const char* name)
{
    return object.hasProperty(juce::Identifier(name));
}

[[nodiscard]] const juce::var& property(const juce::DynamicObject& object, const char* name)
{
    return object.getProperty(juce::Identifier(name));
}

[[nodiscard]] bool readInteger(const juce::var& value, int& result)
{
    if (value.isInt())
    {
        result = static_cast<int>(value);
        return true;
    }
    if (!value.isInt64())
    {
        return false;
    }

    const auto integer = static_cast<juce::int64>(value);
    if (integer < std::numeric_limits<int>::min() || integer > std::numeric_limits<int>::max())
    {
        return false;
    }

    result = static_cast<int>(integer);
    return true;
}

[[nodiscard]] bool readNumber(const juce::var& value, double& result)
{
    if (!value.isInt() && !value.isInt64() && !value.isDouble())
    {
        return false;
    }

    result = static_cast<double>(value);
    return std::isfinite(result);
}

[[nodiscard]] bool readBoolean(const juce::var& value, bool& result)
{
    if (!value.isBool())
    {
        return false;
    }

    result = static_cast<bool>(value);
    return true;
}

[[nodiscard]] bool readString(const juce::var& value, std::string& result, bool allowEmpty = true)
{
    if (!value.isString())
    {
        return false;
    }

    const auto text = value.toString();
    if (!allowEmpty && text.isEmpty())
    {
        return false;
    }

    result = text.toStdString();
    return true;
}

[[nodiscard]] const char* themeName(Theme theme)
{
    return theme == Theme::dark ? "dark" : "light";
}

[[nodiscard]] const char* fullscreenModeName(AppSettings::FullscreenMode mode)
{
    return mode == AppSettings::FullscreenMode::kiosk ? "kiosk" : "normal";
}

[[nodiscard]] bool parseTheme(std::string_view value, Theme& result)
{
    if (value == "dark")
    {
        result = Theme::dark;
        return true;
    }
    if (value == "light")
    {
        result = Theme::light;
        return true;
    }
    return false;
}

[[nodiscard]] bool parseFullscreenMode(std::string_view value, AppSettings::FullscreenMode& result)
{
    if (value == "kiosk")
    {
        result = AppSettings::FullscreenMode::kiosk;
        return true;
    }
    if (value == "normal")
    {
        result = AppSettings::FullscreenMode::normal;
        return true;
    }
    return false;
}

[[nodiscard]] bool
validateImportedWorkspaceNames(const juce::var& workspaceValue, std::string& error)
{
    const auto* workspaces = workspaceValue.getDynamicObject();
    if (workspaces == nullptr || !hasProperty(*workspaces, "named"))
    {
        return true;
    }

    const auto* named = property(*workspaces, "named").getArray();
    if (named == nullptr)
    {
        return true;
    }

    std::unordered_set<std::string> ids;
    std::unordered_set<std::string> names;
    std::unordered_set<std::string> reservedNames;
    for (const auto& builtIn : WorkspaceBuiltIns::all())
    {
        reservedNames.insert(
            juce::String::fromUTF8(builtIn.name.c_str()).trim().toLowerCase().toStdString());
    }

    for (const auto& value : *named)
    {
        const auto* workspace = value.getDynamicObject();
        std::string id;
        std::string name;
        if (workspace == nullptr || !hasProperty(*workspace, "id") ||
            !hasProperty(*workspace, "name") ||
            !readString(property(*workspace, "id"), id, false) ||
            !readString(property(*workspace, "name"), name, false))
        {
            continue;
        }

        const auto foldedName =
            juce::String::fromUTF8(name.c_str()).trim().toLowerCase().toStdString();
        if (!ids.insert(id).second)
        {
            error = std::string(l10n::key("settings.transfer.error.duplicate-workspace-ids"));
            return false;
        }
        if (!names.insert(foldedName).second)
        {
            error = std::string(l10n::key("settings.transfer.error.duplicate-workspace-names"));
            return false;
        }
        if (reservedNames.contains(foldedName))
        {
            error = std::string(l10n::key("settings.transfer.error.reserved-workspace-name"));
            return false;
        }
    }
    return true;
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

// The settings a bundle carries since every setting travels: in range as the
// settings file reads them (AppSettings::load), and the defaults for any one
// missing. A bundle with no `practice` section is from before, and says so.
void readAllSettings(
    const juce::DynamicObject& settings,
    const juce::DynamicObject& appearance,
    SettingsTransferModel& model)
{
    auto& state = model.state;
    model.carriesAllSettings = hasProperty(settings, "practice");

    const auto section = [&settings](const char* name) -> const juce::DynamicObject*
    { return hasProperty(settings, name) ? property(settings, name).getDynamicObject() : nullptr; };
    const auto number = [](const juce::DynamicObject* object, const char* name, double fallback,
                           double lowest, double highest)
    {
        double value = fallback;
        if (object != nullptr && hasProperty(*object, name) &&
            readNumber(property(*object, name), value) && std::isfinite(value))
        {
            return juce::jlimit(lowest, highest, value);
        }
        return fallback;
    };
    const auto boolean = [](const juce::DynamicObject* object, const char* name, bool fallback)
    {
        bool value = fallback;
        if (object != nullptr && hasProperty(*object, name) &&
            readBoolean(property(*object, name), value))
        {
            return value;
        }
        return fallback;
    };
    const auto text = [](const juce::DynamicObject* object, const char* name)
    {
        std::string value;
        if (object != nullptr && hasProperty(*object, name) &&
            readString(property(*object, name), value))
        {
            return juce::String::fromUTF8(value.c_str());
        }
        return juce::String();
    };

    const juce::String language = text(&appearance, "language");
    state.language =
        language.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")
            ? language
            : juce::String();
    state.noteNaming = static_cast<int>(number(&appearance, "noteNaming", 0.0, 0.0, 2.0));
    state.octaveNumbering = static_cast<int>(number(&appearance, "octaveNumbering", 0.0, 0.0, 2.0));
    state.keySignatureFifths =
        static_cast<int>(std::lround(number(&appearance, "keySignature", 0.0, -7.0, 7.0)));
    state.flatsWithoutKey = boolean(&appearance, "flatsWithoutKey", false);

    const auto* metronome = section("metronome");
    state.metronomeEnabled = boolean(metronome, "enabled", AppDefaults::Metronome::enabled);
    state.metronomeVolume = number(metronome, "volume", AppDefaults::Metronome::volume, 0.0, 1.0);
    state.metronomeAccent = boolean(metronome, "accent", AppDefaults::Metronome::accent);
    state.countInBars = static_cast<int>(
        number(metronome, "countInBars", AppDefaults::Metronome::countInBars, 0.0, 4.0));

    const auto* playback = section("playback");
    state.playbackEnabled = boolean(playback, "enabled", AppDefaults::Playback::enabled);
    state.playbackVolume = number(playback, "volume", AppDefaults::Playback::volume, 0.0, 1.0);

    const auto* practice = section("practice");
    state.referencePitchHz =
        number(practice, "referencePitchHz", AppDefaults::referencePitchHz, 415.0, 466.0);
    const auto limits = pitch::orderedLimits(
        {number(
             practice, "nearPerfectCents", AppDefaults::nearPerfectCents,
             AppDefaults::minimumNearPerfectCents, AppDefaults::maximumNearPerfectCents),
         number(
             practice, "inTuneCents", AppDefaults::inTuneCents, AppDefaults::minimumInTuneCents,
             AppDefaults::maximumInTuneCents),
         number(
             practice, "outOfTuneCents", AppDefaults::outOfTuneCents,
             AppDefaults::minimumOutOfTuneCents, AppDefaults::maximumOutOfTuneCents)});
    state.inTuneCents = limits.toleranceCents;
    state.nearPerfectCents = limits.nearPerfectCents;
    state.outOfTuneCents = limits.outOfTuneCents;
    state.minimumLevelDb = number(
        practice, "minimumLevelDb", AppDefaults::minimumLevelDb, AppDefaults::lowestMinimumLevelDb,
        AppDefaults::highestMinimumLevelDb);
    {
        const evaluation::TimingLimits limits = evaluation::orderedTimingLimits(
            {number(
                 practice, "timingRightOnMs", 15.0, evaluation::minimumRightOnMs,
                 evaluation::maximumRightOnMs),
             number(
                 practice, "timingOnTimeMs", 40.0, evaluation::minimumOnTimeMs,
                 evaluation::maximumOnTimeMs),
             number(
                 practice, "timingOffBeatMs", 100.0, evaluation::minimumOffBeatMs,
                 evaluation::maximumOffBeatMs)});
        state.timingRightOnMs = limits.rightOnMs;
        state.timingOnTimeMs = limits.onTimeMs;
        state.timingOffBeatMs = limits.offBeatMs;
    }
    state.keepTargetWhenScoreOpens = boolean(practice, "keepTargetWhenScoreOpens", false);
    state.targetFollowsScore = boolean(practice, "targetFollowsScore", true);
    state.countInWhenRecordingDuringPlayback =
        boolean(practice, "countInWhenRecordingDuringPlayback", false);

    const auto* midi = section("midi");
    state.midiInputDeviceIdentifier = text(midi, "deviceIdentifier");
    state.midiInputDeviceName = text(midi, "deviceName");
    state.midiPedalDownThreshold = static_cast<int>(
        number(midi, "pedalDownThreshold", AppDefaults::Midi::pedalDownThreshold, 0.0, 127.0));
}

[[nodiscard]] juce::var encodeWorkspaceCatalog(const WorkspaceCatalog& catalog)
{
    juce::var parsed;
    const auto serialized = WorkspaceCatalogCodec::encode(catalog);
    const auto parseResult = juce::JSON::parse(toJuceString(serialized), parsed);
    if (parseResult.failed())
    {
        return juce::var(new juce::DynamicObject());
    }
    return parsed;
}
} // namespace

SettingsTransferEncodeResult SettingsTransferCodec::encode(const SettingsTransferModel& model)
{
    if (model.schemaVersion != SettingsTransferModel::currentSchemaVersion)
    {
        return {
            .status = SettingsTransferEncodeStatus::invalidModel,
            .document = std::nullopt,
            .error = std::string(l10n::key("settings.transfer.error.model-unsupported"))};
    }

    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("format", formatMarker);
    root->setProperty("schemaVersion", SettingsTransferModel::currentSchemaVersion);
    root->setProperty("applicationVersion", toJuceString(model.applicationVersion));

    auto settings = std::make_unique<juce::DynamicObject>();

    const auto& state = model.state;
    auto appearance = std::make_unique<juce::DynamicObject>();
    appearance->setProperty("theme", themeName(state.theme));
    appearance->setProperty("textScale", state.textScalePercent);
    appearance->setProperty("language", state.language);
    appearance->setProperty("noteNaming", state.noteNaming);
    appearance->setProperty("octaveNumbering", state.octaveNumbering);
    appearance->setProperty("keySignature", state.keySignatureFifths);
    appearance->setProperty("flatsWithoutKey", state.flatsWithoutKey);
    settings->setProperty("appearance", appearance.release());

    // Every setting travels, the devices too: an import on a machine without
    // them falls back as a missing device always has.
    auto audio = std::make_unique<juce::DynamicObject>();
    audio->setProperty("microphoneMuted", state.microphoneMuted);
    audio->setProperty("inputGain", state.inputGain);
    audio->setProperty("deviceState", state.audioDeviceState);
    settings->setProperty("audio", audio.release());

    auto metronome = std::make_unique<juce::DynamicObject>();
    metronome->setProperty("enabled", state.metronomeEnabled);
    metronome->setProperty("volume", state.metronomeVolume);
    metronome->setProperty("accent", state.metronomeAccent);
    metronome->setProperty("countInBars", state.countInBars);
    settings->setProperty("metronome", metronome.release());

    auto playback = std::make_unique<juce::DynamicObject>();
    playback->setProperty("enabled", state.playbackEnabled);
    playback->setProperty("volume", state.playbackVolume);
    settings->setProperty("playback", playback.release());

    auto practice = std::make_unique<juce::DynamicObject>();
    practice->setProperty("referencePitchHz", state.referencePitchHz);
    practice->setProperty("nearPerfectCents", state.nearPerfectCents);
    practice->setProperty("inTuneCents", state.inTuneCents);
    practice->setProperty("outOfTuneCents", state.outOfTuneCents);
    practice->setProperty("minimumLevelDb", state.minimumLevelDb);
    practice->setProperty("timingRightOnMs", state.timingRightOnMs);
    practice->setProperty("timingOnTimeMs", state.timingOnTimeMs);
    practice->setProperty("timingOffBeatMs", state.timingOffBeatMs);
    practice->setProperty("keepTargetWhenScoreOpens", state.keepTargetWhenScoreOpens);
    practice->setProperty("targetFollowsScore", state.targetFollowsScore);
    practice->setProperty(
        "countInWhenRecordingDuringPlayback", state.countInWhenRecordingDuringPlayback);
    settings->setProperty("practice", practice.release());

    auto midi = std::make_unique<juce::DynamicObject>();
    midi->setProperty("deviceIdentifier", state.midiInputDeviceIdentifier);
    midi->setProperty("deviceName", state.midiInputDeviceName);
    midi->setProperty("pedalDownThreshold", state.midiPedalDownThreshold);
    settings->setProperty("midi", midi.release());

    auto window = std::make_unique<juce::DynamicObject>();
    window->setProperty("fullscreenMode", fullscreenModeName(model.state.fullscreenMode));
    window->setProperty("settingsBounds", model.state.settingsBounds);

    auto floatingBounds = std::make_unique<juce::DynamicObject>();
    floatingBounds->setProperty("tuner", model.state.tunerBounds);
    floatingBounds->setProperty("spectrogram", model.state.spectrogramBounds);
    floatingBounds->setProperty("harmonicAnalyzer", model.state.harmonicBounds);
    window->setProperty("floatingBounds", floatingBounds.release());
    settings->setProperty("window", window.release());

    auto feedback = std::make_unique<juce::DynamicObject>();
    feedback->setProperty("invitationsDisabled", model.feedbackInvitationsDisabled);
    settings->setProperty("feedback", feedback.release());

    settings->setProperty("workspaces", encodeWorkspaceCatalog(model.state.workspaceCatalog));
    root->setProperty("settings", settings.release());

    auto document = juce::JSON::toString(juce::var(root.release()), true, 17).toStdString();
    if (document.size() > maximumDocumentBytes)
    {
        return {
            .status = SettingsTransferEncodeStatus::tooLarge,
            .document = std::nullopt,
            .error = std::string(l10n::key("settings.transfer.error.too-large"))};
    }

    return {
        .status = SettingsTransferEncodeStatus::encoded,
        .document = std::move(document),
        .error = {}};
}

SettingsTransferDecodeResult SettingsTransferCodec::decode(
    std::string_view document,
    const std::vector<WorkspaceBounds>& displayAreas,
    const ToolCatalog& registry)
{
    if (document.size() > maximumDocumentBytes)
    {
        return {
            .status = SettingsTransferDecodeStatus::tooLarge,
            .model = std::nullopt,
            .error = std::string(l10n::key("settings.transfer.error.too-large"))};
    }

    juce::var parsed;
    const auto parseResult = juce::JSON::parse(toJuceString(document), parsed);
    if (parseResult.failed())
    {
        return {
            .status = SettingsTransferDecodeStatus::invalid,
            .model = std::nullopt,
            .error = std::string(l10n::key("settings.transfer.error.not-json"))};
    }

    const auto* root = parsed.getDynamicObject();
    std::string format;
    int schemaVersion = 0;
    if (root == nullptr || !hasProperty(*root, "format") || !hasProperty(*root, "schemaVersion") ||
        !hasProperty(*root, "applicationVersion") || !hasProperty(*root, "settings") ||
        !readString(property(*root, "format"), format, false) ||
        !readInteger(property(*root, "schemaVersion"), schemaVersion))
    {
        return {
            .status = SettingsTransferDecodeStatus::invalid,
            .model = std::nullopt,
            .error = std::string(l10n::key("settings.transfer.error.missing-fields"))};
    }

    if (format != formatMarker)
    {
        return {
            .status = SettingsTransferDecodeStatus::invalid,
            .model = std::nullopt,
            .error = std::string(l10n::key("settings.transfer.error.not-a-settings-file"))};
    }
    if (schemaVersion > SettingsTransferModel::currentSchemaVersion)
    {
        return {
            .status = SettingsTransferDecodeStatus::newerSchema,
            .model = std::nullopt,
            .error = std::string(l10n::key("settings.transfer.error.newer"))};
    }
    if (schemaVersion < 1)
    {
        return {
            .status = SettingsTransferDecodeStatus::invalid,
            .model = std::nullopt,
            .error = std::string(l10n::key("settings.transfer.error.unsupported"))};
    }

    SettingsTransferModel model;
    model.schemaVersion = SettingsTransferModel::currentSchemaVersion;
    if (!readString(property(*root, "applicationVersion"), model.applicationVersion))
    {
        return {
            .status = SettingsTransferDecodeStatus::invalid,
            .model = std::nullopt,
            .error = std::string(l10n::key("settings.transfer.error.version"))};
    }

    const auto* settings = property(*root, "settings").getDynamicObject();
    if (settings == nullptr || !hasProperty(*settings, "appearance") ||
        !hasProperty(*settings, "audio") || !hasProperty(*settings, "window") ||
        !hasProperty(*settings, "feedback") || !hasProperty(*settings, "workspaces"))
    {
        return {
            .status = SettingsTransferDecodeStatus::invalid,
            .model = std::nullopt,
            .error = std::string(l10n::key("settings.transfer.error.incomplete"))};
    }

    const auto* appearance = property(*settings, "appearance").getDynamicObject();
    const auto* audio = property(*settings, "audio").getDynamicObject();
    const auto* window = property(*settings, "window").getDynamicObject();
    const auto* feedback = property(*settings, "feedback").getDynamicObject();
    if (appearance == nullptr || audio == nullptr || window == nullptr || feedback == nullptr)
    {
        return {
            .status = SettingsTransferDecodeStatus::invalid,
            .model = std::nullopt,
            .error = std::string(l10n::key("settings.transfer.error.sections"))};
    }

    std::string theme;
    bool microphoneMuted = false;
    double inputGain = 0.0;
    std::string deviceState;
    std::string fullscreenMode;
    std::string settingsBounds;
    bool invitationsDisabled = false;
    Theme parsedTheme = Theme::light;
    AppSettings::FullscreenMode parsedFullscreenMode = AppSettings::FullscreenMode::normal;

    if (!hasProperty(*appearance, "theme") ||
        !readString(property(*appearance, "theme"), theme, false) ||
        !parseTheme(theme, parsedTheme) || !hasProperty(*audio, "microphoneMuted") ||
        !readBoolean(property(*audio, "microphoneMuted"), microphoneMuted) ||
        !hasProperty(*audio, "inputGain") ||
        !readNumber(property(*audio, "inputGain"), inputGain) ||
        !hasProperty(*audio, "deviceState") ||
        !readString(property(*audio, "deviceState"), deviceState) ||
        !hasProperty(*window, "fullscreenMode") ||
        !readString(property(*window, "fullscreenMode"), fullscreenMode, false) ||
        !parseFullscreenMode(fullscreenMode, parsedFullscreenMode) ||
        !hasProperty(*window, "settingsBounds") ||
        !readString(property(*window, "settingsBounds"), settingsBounds) ||
        !hasProperty(*feedback, "invitationsDisabled") ||
        !readBoolean(property(*feedback, "invitationsDisabled"), invitationsDisabled))
    {
        return {
            .status = SettingsTransferDecodeStatus::invalid,
            .model = std::nullopt,
            .error = std::string(l10n::key("settings.transfer.error.fields"))};
    }

    const auto* floatingBounds =
        hasProperty(*window, "floatingBounds")
            ? property(*window, "floatingBounds").getDynamicObject()
            : nullptr;
    std::string tunerBounds;
    std::string spectrogramBounds;
    std::string harmonicBounds;
    if (floatingBounds != nullptr)
    {
        if (hasProperty(*floatingBounds, "tuner") &&
            !readString(property(*floatingBounds, "tuner"), tunerBounds))
        {
            return {
                .status = SettingsTransferDecodeStatus::invalid,
                .model = std::nullopt,
                .error = std::string(l10n::key("settings.transfer.error.tuner-bounds"))};
        }
        if (hasProperty(*floatingBounds, "spectrogram") &&
            !readString(property(*floatingBounds, "spectrogram"), spectrogramBounds))
        {
            return {
                .status = SettingsTransferDecodeStatus::invalid,
                .model = std::nullopt,
                .error = std::string(l10n::key("settings.transfer.error.spectrogram-bounds"))};
        }
        if (hasProperty(*floatingBounds, "harmonicAnalyzer") &&
            !readString(property(*floatingBounds, "harmonicAnalyzer"), harmonicBounds))
        {
            return {
                .status = SettingsTransferDecodeStatus::invalid,
                .model = std::nullopt,
                .error = std::string(l10n::key("settings.transfer.error.harmonics-bounds"))};
        }
    }

    const auto workspaceDocument =
        juce::JSON::toString(property(*settings, "workspaces"), true, 17).toStdString();
    std::string workspaceValidationError;
    if (!validateImportedWorkspaceNames(
            property(*settings, "workspaces"), workspaceValidationError))
    {
        return {
            .status = SettingsTransferDecodeStatus::invalid,
            .model = std::nullopt,
            .error = std::move(workspaceValidationError)};
    }
    const auto workspaceDecode =
        WorkspaceCatalogCodec::decode(workspaceDocument, displayAreas, registry);
    if (workspaceDecode.status == WorkspaceCatalogDecodeStatus::newerSchema)
    {
        return {
            .status = SettingsTransferDecodeStatus::newerSchema,
            .model = std::nullopt,
            .error = workspaceDecode.error};
    }
    if (!workspaceDecode.catalog.has_value())
    {
        return {
            .status = SettingsTransferDecodeStatus::invalid,
            .model = std::nullopt,
            .error = workspaceDecode.error};
    }

    model.state.theme = parsedTheme;
    // Optional: a bundle from before the setting carries none, and imports at
    // 100. A value that is there but not a number is as good as missing.
    {
        double textScale = static_cast<double>(appearance::TextScale::defaultPercent);
        if (hasProperty(*appearance, "textScale"))
        {
            static_cast<void>(readNumber(property(*appearance, "textScale"), textScale));
        }
        model.state.textScalePercent = appearance::TextScale::snapped(textScale);
    }
    readAllSettings(*settings, *appearance, model);
    model.state.microphoneMuted = microphoneMuted;
    model.state.inputGain = inputGain;
    model.state.audioDeviceState = juce::String::fromUTF8(deviceState.c_str());
    model.state.fullscreenMode = parsedFullscreenMode;
    model.state.settingsBounds = juce::String::fromUTF8(settingsBounds.c_str());
    model.state.tunerBounds = juce::String::fromUTF8(tunerBounds.c_str());
    model.state.spectrogramBounds = juce::String::fromUTF8(spectrogramBounds.c_str());
    model.state.harmonicBounds = juce::String::fromUTF8(harmonicBounds.c_str());
    model.state.workspaceCatalog = *workspaceDecode.catalog;
    model.state.recentTool = recentTool(model.state.workspaceCatalog);
    model.feedbackInvitationsDisabled = invitationsDisabled;

    return {.status = SettingsTransferDecodeStatus::loaded, .model = std::move(model), .error = {}};
}

SettingsTransferModel SettingsTransferCodec::withLocalPreferences(
    SettingsTransferModel imported,
    const AppSettings::State& local)
{
    auto& state = imported.state;
    // The note being practised is what this machine is doing now, not a
    // preference, so no bundle carries it and an import keeps it.
    state.chosenTargetSounding = local.chosenTargetSounding;
    state.chosenTargetWritten = local.chosenTargetWritten;
    if (imported.carriesAllSettings)
    {
        return imported;
    }
    state.language = local.language;
    state.noteNaming = local.noteNaming;
    state.octaveNumbering = local.octaveNumbering;
    state.keySignatureFifths = local.keySignatureFifths;
    state.flatsWithoutKey = local.flatsWithoutKey;
    state.metronomeEnabled = local.metronomeEnabled;
    state.metronomeVolume = local.metronomeVolume;
    state.metronomeAccent = local.metronomeAccent;
    state.countInBars = local.countInBars;
    state.playbackEnabled = local.playbackEnabled;
    state.playbackVolume = local.playbackVolume;
    state.referencePitchHz = local.referencePitchHz;
    state.nearPerfectCents = local.nearPerfectCents;
    state.inTuneCents = local.inTuneCents;
    state.outOfTuneCents = local.outOfTuneCents;
    state.minimumLevelDb = local.minimumLevelDb;
    state.timingRightOnMs = local.timingRightOnMs;
    state.timingOnTimeMs = local.timingOnTimeMs;
    state.timingOffBeatMs = local.timingOffBeatMs;
    state.keepTargetWhenScoreOpens = local.keepTargetWhenScoreOpens;
    state.targetFollowsScore = local.targetFollowsScore;
    state.countInWhenRecordingDuringPlayback = local.countInWhenRecordingDuringPlayback;
    state.midiInputDeviceIdentifier = local.midiInputDeviceIdentifier;
    state.midiInputDeviceName = local.midiInputDeviceName;
    state.midiPedalDownThreshold = local.midiPedalDownThreshold;
    // A bundle from before outputs travelled names none: this machine's stays.
    if (const auto local_ = juce::parseXML(local.audioDeviceState))
    {
        if (auto merged = AudioDeviceStateTransfer::withLocalOutputDevice(
                state.audioDeviceState, local_.get()))
        {
            state.audioDeviceState = merged->toString(juce::XmlElement::TextFormat().singleLine());
        }
    }
    imported.carriesAllSettings = true;
    return imported;
}
