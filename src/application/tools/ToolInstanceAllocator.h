#pragma once

#include "ToolCatalog.h"
#include "ToolInstanceId.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string_view>

// Which instance id the next open of a tool should use.
//
// Split out of MainComponent so the instance policy -- the rule that decides
// whether opening an already-open tool focuses it or duplicates it -- can be
// tested without a display. The shell supplies `isLive`, which answers whether
// an instance is currently open.
namespace ToolInstanceAllocator
{
// Nullopt when the tool is not registered. Otherwise the id to open:
//
//  - a single-instance tool always returns ordinal 1, so reopening lands on the
//    same entry, and the same saved bounds and settings, it had before;
//  - a multi-instance tool returns the lowest ordinal not currently live, which
//    reuses the slot a closed instance gave up rather than counting upwards
//    forever.
[[nodiscard]] inline std::optional<ToolInstanceId> nextInstanceId(
    const ToolCatalog& catalog,
    std::string_view toolId,
    const std::function<bool(const ToolInstanceId&)>& isLive)
{
    const auto* definition = catalog.find(toolId);
    if (definition == nullptr)
    {
        return std::nullopt;
    }

    if (definition->instancePolicy == ToolInstancePolicy::single || !isLive)
    {
        return ToolInstanceId::forOrdinal(toolId, 1);
    }

    for (std::size_t ordinal = 1;; ++ordinal)
    {
        auto candidate = ToolInstanceId::forOrdinal(toolId, ordinal);
        if (!isLive(candidate))
        {
            return candidate;
        }
    }
}

// The instance "Open or focus" means: the first live instance of the tool, or
// ordinal 1 when none is live. Unlike nextInstanceId, this never mints a new
// ordinal, so for a multi-instance tool "Open or focus" focuses what is there
// instead of adding another copy each time it is chosen. `highestOrdinal`
// bounds the search; the shell passes the largest ordinal it has live.
[[nodiscard]] inline std::optional<ToolInstanceId> instanceToFocus(
    const ToolCatalog& catalog,
    std::string_view toolId,
    const std::function<bool(const ToolInstanceId&)>& isLive,
    std::size_t highestOrdinal)
{
    if (catalog.find(toolId) == nullptr)
    {
        return std::nullopt;
    }

    if (isLive)
    {
        for (std::size_t ordinal = 1; ordinal <= highestOrdinal; ++ordinal)
        {
            auto candidate = ToolInstanceId::forOrdinal(toolId, ordinal);
            if (isLive(candidate))
            {
                return candidate;
            }
        }
    }
    return ToolInstanceId::forOrdinal(toolId, 1);
}

// Whether "Open another" can create an instance now. Never for a
// single-instance tool; for a tool that takes an audio consumer, only while the
// shared input service has a slot free -- addListener cannot fail loudly on the
// audio side, so the shell refuses first and says why.
[[nodiscard]] inline bool
mayOpenAnother(const ToolDefinition& definition, std::size_t freeConsumerSlots) noexcept
{
    if (definition.instancePolicy != ToolInstancePolicy::multi)
    {
        return false;
    }
    return !definition.usesAudioInput || freeConsumerSlots > 0;
}
} // namespace ToolInstanceAllocator
