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

    // Whether the bundle carries every setting -- the language, note naming,
    // metronome, playback, practice and MIDI ones -- or only what a bundle
    // from before they travelled did. An import of an older one keeps this
    // machine's values for those rather than resetting them
    // (SettingsTransferCodec::withLocalPreferences).
    bool carriesAllSettings = true;

    // No operator== here on purpose. `AppSettings::State` compares nothing of
    // its own, so a defaulted comparison on this type is implicitly deleted --
    // present in the header, absent at every call site, and it fails to
    // compile with an error that points at the wrong type when someone
    // reaches for it. See #160. Giving `State` (and `TunerSettings`,
    // `RecentTool`, `WorkspaceCatalog`) a real comparison so a round-trip
    // equality assertion becomes possible is a separate, larger decision.
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

    // `imported`, with this machine's `local` values for what it does not
    // carry: the chosen target note always, and every setting an older
    // bundle predates.
    [[nodiscard]] static SettingsTransferModel
    withLocalPreferences(SettingsTransferModel imported, const AppSettings::State& local);
};
