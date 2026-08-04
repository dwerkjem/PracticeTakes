#include <catch2/catch_test_macros.hpp>

#include "application/tools/BuiltInToolCatalog.h"
#include "application/tools/ToolCatalog.h"

TEST_CASE("the built-in catalog declares every shipped tool", "[tools][catalog]")
{
    const auto& catalog = builtInToolCatalog();

    REQUIRE(catalog.rejectedIds().empty());
    REQUIRE(catalog.tools().size() == 3);

    for (const auto& definition : catalog.tools())
    {
        INFO("tool id: " << definition.id);
        CHECK_FALSE(definition.displayName.empty());
        CHECK(definition.preferredSize.width > 0);
        CHECK(definition.preferredSize.height > 0);
    }
}

TEST_CASE("every shipped tool is single-instance", "[tools][catalog]")
{
    // Multi-instance is declared but deliberately unexercised in production.
    // If this fails, a tool started duplicating without the workspace model
    // having been widened for it.
    for (const auto& definition : builtInToolCatalog().tools())
    {
        INFO("tool id: " << definition.id);
        CHECK(definition.instancePolicy == ToolInstancePolicy::single);
    }
}

TEST_CASE("historical aliases still resolve to their tool", "[tools][catalog]")
{
    const auto& catalog = builtInToolCatalog();

    CHECK(catalog.resolve("tuner") == "tuner");
    CHECK(catalog.resolve("pitch-detector") == "tuner");
    CHECK(catalog.resolve("spectrum") == "spectrogram");
    CHECK(catalog.resolve("harmonics") == "harmonic-analyzer");
    CHECK(catalog.resolve("harmonicAnalyzer") == "harmonic-analyzer");
    CHECK_FALSE(catalog.resolve("no-such-tool").has_value());
}

TEST_CASE("a tool id containing a separator is rejected", "[tools][catalog]")
{
    // '#' separates the tool from the ordinal in an instance id, so allowing
    // it in a tool id would make "a#2" ambiguous.
    CHECK_FALSE(ToolCatalog::isValidToolId("a#2"));
    CHECK_FALSE(ToolCatalog::isValidToolId(""));
    CHECK(ToolCatalog::isValidToolId("harmonic-analyzer"));

    const ToolCatalog catalog({
        ToolDefinition{"good", "Good", {}, ToolInstancePolicy::single, std::nullopt, {100, 100}},
        ToolDefinition{"bad#1", "Bad", {}, ToolInstancePolicy::single, std::nullopt, {100, 100}},
    });

    CHECK(catalog.tools().size() == 1);
    CHECK(catalog.rejectedIds() == std::vector<std::string>{"bad#1"});
    CHECK(catalog.find("bad#1") == nullptr);
}

TEST_CASE("a duplicate tool id is rejected rather than shadowing", "[tools][catalog]")
{
    const ToolCatalog catalog({
        ToolDefinition{"tuner", "First", {}, ToolInstancePolicy::single, std::nullopt, {100, 100}},
        ToolDefinition{"tuner", "Second", {}, ToolInstancePolicy::single, std::nullopt, {200, 200}},
    });

    REQUIRE(catalog.tools().size() == 1);
    CHECK(catalog.tools().front().displayName == "First");
    CHECK(catalog.rejectedIds() == std::vector<std::string>{"tuner"});
}

TEST_CASE("an instance resolves to the tool that declared it", "[tools][catalog]")
{
    const auto& catalog = builtInToolCatalog();

    const auto* first = catalog.findForInstance(ToolInstanceId::forOrdinal("tuner", 1));
    const auto* second = catalog.findForInstance(ToolInstanceId::forOrdinal("tuner", 2));

    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    // Two instances share one definition: one name, one factory, one codec.
    CHECK(first == second);
    CHECK(first->id == "tuner");

    CHECK(catalog.findForInstance(ToolInstanceId("no-such-tool")) == nullptr);
    CHECK(catalog.findForInstance(ToolInstanceId("tuner#")) == nullptr);
}

TEST_CASE("a saved instance id resolves through an alias keeping its ordinal", "[tools][catalog]")
{
    const auto& catalog = builtInToolCatalog();

    const auto resolved = catalog.resolveInstance(ToolInstanceId("harmonics"));
    REQUIRE(resolved.has_value());
    CHECK(resolved->value() == "harmonic-analyzer");

    const auto resolvedSecond = catalog.resolveInstance(ToolInstanceId("harmonics#3"));
    REQUIRE(resolvedSecond.has_value());
    CHECK(resolvedSecond->value() == "harmonic-analyzer#3");

    CHECK_FALSE(catalog.resolveInstance(ToolInstanceId("no-such-tool")).has_value());
    CHECK_FALSE(catalog.resolveInstance(ToolInstanceId("tuner#x")).has_value());
}
