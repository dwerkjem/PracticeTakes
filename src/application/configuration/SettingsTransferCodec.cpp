#include "SettingsTransferCodec.h"

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
            error = "Settings transfer contains duplicate workspace IDs";
            return false;
        }
        if (!names.insert(foldedName).second)
        {
            error = "Settings transfer contains duplicate workspace names";
            return false;
        }
        if (reservedNames.contains(foldedName))
        {
            error = "Settings transfer contains a reserved built-in workspace name";
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
            .error = "Settings transfer model schema is unsupported"};
    }

    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("format", formatMarker);
    root->setProperty("schemaVersion", SettingsTransferModel::currentSchemaVersion);
    root->setProperty("applicationVersion", toJuceString(model.applicationVersion));

    auto settings = std::make_unique<juce::DynamicObject>();

    auto appearance = std::make_unique<juce::DynamicObject>();
    appearance->setProperty("theme", themeName(model.state.theme));
    settings->setProperty("appearance", appearance.release());

    auto audio = std::make_unique<juce::DynamicObject>();
    audio->setProperty("microphoneMuted", model.state.microphoneMuted);
    audio->setProperty("inputGain", model.state.inputGain);
    audio->setProperty("deviceState", model.state.audioDeviceState);
    settings->setProperty("audio", audio.release());

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
            .error = "Settings transfer document exceeds the 4 MiB limit"};
    }

    return {
        .status = SettingsTransferEncodeStatus::encoded,
        .document = std::move(document),
        .error = {}};
}

SettingsTransferDecodeResult SettingsTransferCodec::decode(
    std::string_view document,
    const std::vector<WorkspaceBounds>& displayAreas,
    const WorkspaceToolRegistry& registry)
{
    if (document.size() > maximumDocumentBytes)
    {
        return {
            .status = SettingsTransferDecodeStatus::tooLarge,
            .model = std::nullopt,
            .error = "Settings transfer document exceeds the 4 MiB limit"};
    }

    juce::var parsed;
    const auto parseResult = juce::JSON::parse(toJuceString(document), parsed);
    if (parseResult.failed())
    {
        return {
            .status = SettingsTransferDecodeStatus::invalid,
            .model = std::nullopt,
            .error = parseResult.getErrorMessage().toStdString()};
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
            .error = "Settings transfer document is missing required fields"};
    }

    if (format != formatMarker)
    {
        return {
            .status = SettingsTransferDecodeStatus::invalid,
            .model = std::nullopt,
            .error = "Settings transfer format marker is invalid"};
    }
    if (schemaVersion > SettingsTransferModel::currentSchemaVersion)
    {
        return {
            .status = SettingsTransferDecodeStatus::newerSchema,
            .model = std::nullopt,
            .error = "Settings transfer uses a newer schema"};
    }
    if (schemaVersion < 1)
    {
        return {
            .status = SettingsTransferDecodeStatus::invalid,
            .model = std::nullopt,
            .error = "Settings transfer schema is unsupported"};
    }

    SettingsTransferModel model;
    model.schemaVersion = SettingsTransferModel::currentSchemaVersion;
    if (!readString(property(*root, "applicationVersion"), model.applicationVersion))
    {
        return {
            .status = SettingsTransferDecodeStatus::invalid,
            .model = std::nullopt,
            .error = "Settings transfer applicationVersion is invalid"};
    }

    const auto* settings = property(*root, "settings").getDynamicObject();
    if (settings == nullptr || !hasProperty(*settings, "appearance") ||
        !hasProperty(*settings, "audio") || !hasProperty(*settings, "window") ||
        !hasProperty(*settings, "feedback") || !hasProperty(*settings, "workspaces"))
    {
        return {
            .status = SettingsTransferDecodeStatus::invalid,
            .model = std::nullopt,
            .error = "Settings transfer settings section is incomplete"};
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
            .error = "Settings transfer sections must be objects"};
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
            .error = "Settings transfer primitive fields are invalid"};
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
                .error = "Settings transfer tuner floating bounds are invalid"};
        }
        if (hasProperty(*floatingBounds, "spectrogram") &&
            !readString(property(*floatingBounds, "spectrogram"), spectrogramBounds))
        {
            return {
                .status = SettingsTransferDecodeStatus::invalid,
                .model = std::nullopt,
                .error = "Settings transfer spectrogram floating bounds are invalid"};
        }
        if (hasProperty(*floatingBounds, "harmonicAnalyzer") &&
            !readString(property(*floatingBounds, "harmonicAnalyzer"), harmonicBounds))
        {
            return {
                .status = SettingsTransferDecodeStatus::invalid,
                .model = std::nullopt,
                .error = "Settings transfer harmonic floating bounds are invalid"};
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
