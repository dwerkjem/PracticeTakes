#pragma once

#include "../../../application/configuration/AppDefaults.h"
#include "../../../application/tools/ToolSettingsPayload.h"

#include <optional>

// Serialises the tuner's settings for storage in a workspace.
//
// Lives with the tuner rather than in the shell because the shell must not know
// what any tool's settings mean -- it only moves opaque payloads between a tool
// and the workspace it was captured into.
namespace TunerSettingsCodec
{
[[nodiscard]] ToolSettingsPayload encode(const AppDefaults::TunerSettings& settings);

// Returns defaults for a missing or wrong-version payload, and nullopt for one
// that claims the right version but does not parse. The distinction matters:
// the first is a tuner that was never configured, the second is corruption the
// caller should refuse to restore from rather than silently paper over.
[[nodiscard]] std::optional<AppDefaults::TunerSettings>
decode(const std::optional<ToolSettingsPayload>& payload);
} // namespace TunerSettingsCodec
