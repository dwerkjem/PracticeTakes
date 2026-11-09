#include <catch2/catch_test_macros.hpp>

#include "application/tools/BuiltInToolCatalog.h"
#include "features/level/InputLevelSettings.h"

using namespace input_level;

TEST_CASE("the catalog restores what the Input Level saves", "[level][settings]")
{
    // The shell discards a payload whose version is not the tool's
    // settingsVersion, so a mismatch would forget every orientation.
    const ToolDefinition* definition = builtInToolCatalog().find("input-level");

    REQUIRE(definition != nullptr);
    CHECK(definition->settingsVersion == std::optional<int>(settingsVersion));
}

TEST_CASE("each orientation comes back as it was saved", "[level][settings]")
{
    for (const auto orientation :
         {Orientation::automatic, Orientation::horizontal, Orientation::vertical})
    {
        CHECK(decode(encode(orientation)) == orientation);
    }
}

TEST_CASE("anything unreadable is automatic", "[level][settings]")
{
    CHECK(decode({settingsVersion + 1, "orientation=vertical"}) == Orientation::automatic);
    CHECK(decode({settingsVersion, "orientation=diagonal"}) == Orientation::automatic);
    CHECK(decode({settingsVersion, ""}) == Orientation::automatic);
}

TEST_CASE("automatic follows the pane, a choice overrides it", "[level][settings]")
{
    CHECK(drawsVertically(Orientation::automatic, true));
    CHECK_FALSE(drawsVertically(Orientation::automatic, false));
    CHECK(drawsVertically(Orientation::vertical, false));
    CHECK_FALSE(drawsVertically(Orientation::horizontal, true));
}
