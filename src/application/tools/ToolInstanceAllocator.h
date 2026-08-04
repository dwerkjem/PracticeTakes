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
} // namespace ToolInstanceAllocator
