#include <catch2/catch_test_macros.hpp>

#include "application/tools/ToolMenuEntries.h"

#include <string>
#include <vector>

// How a tool's menu entries are numbered when shown, and found again when one
// is chosen (score-part-selection design.md decision 4). The menu is built and
// read back by the same walk, so these pin the walk.
using tool_menu::MenuEntry;

namespace
{
[[nodiscard]] MenuEntry leaf(const juce::String& label, std::string& chosen)
{
    return MenuEntry{label, [&chosen, label] { chosen = label.toStdString(); }};
}
} // namespace

TEST_CASE("a flat menu numbers each entry by its position", "[tools][menu]")
{
    // What every tool's menu was before submenus existed, and still is.
    std::string chosen;
    const std::vector<MenuEntry> entries{
        leaf("Page", chosen), leaf("Vertical", chosen), leaf("Horizontal", chosen)};

    REQUIRE(tool_menu::choosableCount(entries) == 3);

    for (std::size_t number = 0; number < entries.size(); ++number)
    {
        const MenuEntry* entry = tool_menu::entryNumbered(entries, number);
        REQUIRE(entry != nullptr);
        CHECK(entry->label == entries.at(number).label);
    }

    CHECK(tool_menu::entryNumbered(entries, 3) == nullptr);
}

TEST_CASE("a submenu's entries are numbered where they are shown", "[tools][menu]")
{
    // Page, then Parts > (Voice, Piano), then Enlarge: the heading takes no
    // number, its entries take the next ones, and what follows carries on.
    std::string chosen;

    MenuEntry parts{"Parts", {}};
    parts.submenu = {leaf("Voice", chosen), leaf("Piano", chosen)};

    const std::vector<MenuEntry> entries{leaf("Page", chosen), parts, leaf("Enlarge", chosen)};

    REQUIRE(tool_menu::choosableCount(entries) == 4);

    const std::vector<std::string> expected{"Page", "Voice", "Piano", "Enlarge"};

    for (std::size_t number = 0; number < expected.size(); ++number)
    {
        const MenuEntry* entry = tool_menu::entryNumbered(entries, number);
        REQUIRE(entry != nullptr);

        chosen.clear();
        entry->action();
        CHECK(chosen == expected.at(number));
    }
}

TEST_CASE("an entry is enabled unless it says otherwise", "[tools][menu]")
{
    std::string chosen;
    MenuEntry last = leaf("Piano", chosen);

    CHECK(last.enabled);
    CHECK(last.submenu.empty());

    last.enabled = false;
    const std::vector<MenuEntry> entries{last};

    // Still numbered and found, so the menu shows it greyed rather than
    // shifting every number after it; the button refuses to run it.
    const MenuEntry* entry = tool_menu::entryNumbered(entries, 0);
    REQUIRE(entry != nullptr);
    CHECK_FALSE(entry->enabled);
}
