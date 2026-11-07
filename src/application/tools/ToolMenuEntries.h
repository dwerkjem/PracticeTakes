#pragma once

#include "ToolComponent.h"

#include <cstddef>
#include <vector>

// How a tool's menu entries are numbered when shown, so a chosen number finds
// its entry again (score-part-selection design.md decision 4).
//
// Only entries that can be chosen -- the leaves, which have no submenu -- take
// a number, counted depth-first in the order they are shown: an entry, then
// everything in its submenu, then the next. A flat list numbers exactly as it
// always did, each entry by its own position. The menu and the lookup share
// `forEachChoosable`, so they cannot disagree about the order.
namespace tool_menu
{
using MenuEntry = ToolComponent::MenuEntry;

// Calls `visit(entry, number)` for every choosable entry, depth-first.
template <typename Visit>
void forEachChoosable(const std::vector<MenuEntry>& entries, Visit&& visit)
{
    std::size_t number = 0;

    const auto walk =
        [&visit, &number](const std::vector<MenuEntry>& level, const auto& self) -> void
    {
        for (const MenuEntry& entry : level)
        {
            if (entry.submenu.empty())
            {
                visit(entry, number++);
            }
            else
            {
                self(entry.submenu, self);
            }
        }
    };

    walk(entries, walk);
}

// The choosable entry with this number, or nothing past the end.
[[nodiscard]] inline const MenuEntry*
entryNumbered(const std::vector<MenuEntry>& entries, std::size_t wanted)
{
    const MenuEntry* found = nullptr;

    forEachChoosable(
        entries,
        [&found, wanted](const MenuEntry& entry, std::size_t number)
        {
            if (number == wanted)
            {
                found = &entry;
            }
        });

    return found;
}

// How many entries take a number.
[[nodiscard]] inline std::size_t choosableCount(const std::vector<MenuEntry>& entries)
{
    std::size_t count = 0;
    forEachChoosable(entries, [&count](const MenuEntry&, std::size_t) { ++count; });

    return count;
}
} // namespace tool_menu
