#pragma once

#include "../shell/ui/workspace/model/WorkspaceCatalogCodec.h"
#include "SettingsPersistence.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

enum class SettingsTransferDecodeStatus
{
    loaded,
    invalid,
    newerSchema,
    tooLarge
};

enum class SettingsTransferEncodeStatus
{
    encoded,
    invalidModel,
    tooLarge
};

struct SettingsTransferModel
{
    static constexpr int currentSchemaVersion = 2;

    int schemaVersion = currentSchemaVersion;
    std::string applicationVersion;
    AppSettings::State state;
    bool feedbackInvitationsDisabled = false;

    bool operator==(const SettingsTransferModel&) const = default;
};

struct SettingsTransferDecodeResult
{
    SettingsTransferDecodeStatus status = SettingsTransferDecodeStatus::invalid;
    std::optional<SettingsTransferModel> model;
    std::string error;
};

struct SettingsTransferEncodeResult
{
    SettingsTransferEncodeStatus status = SettingsTransferEncodeStatus::invalidModel;
    std::optional<std::string> document;
    std::string error;
};

class SettingsTransferCodec
{
  public:
    static constexpr std::size_t maximumDocumentBytes = 4 * 1024 * 1024;
    static constexpr const char* formatMarker = "practice-takes-settings";

    [[nodiscard]] static SettingsTransferEncodeResult encode(const SettingsTransferModel& model);

    [[nodiscard]] static SettingsTransferDecodeResult decode(
        std::string_view document,
        const std::vector<WorkspaceBounds>& displayAreas,
        const ToolCatalog& registry = builtInToolCatalog());
};
