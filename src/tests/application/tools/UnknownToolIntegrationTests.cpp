#include <catch2/catch_test_macros.hpp>

#include "application/shell/ui/workspace/model/WorkspaceNormalizer.h"
#include "application/shell/ui/workspace/model/WorkspaceSnapshotApply.h"
#include "application/shell/ui/workspace/model/WorkspaceSnapshotCapture.h"
#include "application/tools/ToolInstanceAllocator.h"

#include <string>
#include <vector>

// The acceptance criterion behind this whole change: "a new tool can be added
// without editing the main window in several unrelated places."
//
// Every tool below is invented here and named nowhere else in the codebase --
// not in the catalog, not in the shell, not in the workspace model. If the
// workspace machinery still places, restores, and persists them, then it is
// genuinely driven by registration rather than by a fixed roster.
namespace
{
constexpr auto inventedId = "flux-capacitor";
constexpr auto inventedAlias = "flux";

[[nodiscard]] ToolCatalog catalogWithInventedTool()
{
    return ToolCatalog({
        ToolDefinition{
            inventedId,
            "Flux Capacitor",
            {inventedAlias},
            ToolInstancePolicy::single,
            7,
            {640, 480}},
    });
}
} // namespace

TEST_CASE("a tool the shell has never heard of is placeable", "[tools][registration]")
{
    const auto catalog = catalogWithInventedTool();
    const auto isLive = [](const ToolInstanceId&) { return false; };

    const auto instance = ToolInstanceAllocator::nextInstanceId(catalog, inventedId, isLive);
    REQUIRE(instance.has_value());
    CHECK(instance->value() == inventedId);

    const auto* definition = catalog.findForInstance(*instance);
    REQUIRE(definition != nullptr);
    CHECK(definition->displayName == "Flux Capacitor");
    CHECK(definition->preferredSize == ToolPreferredSize{640, 480});
}

TEST_CASE("a workspace restores a tool the shell has never heard of", "[tools][registration]")
{
    const auto catalog = catalogWithInventedTool();

    WorkspaceSnapshot snapshot;
    snapshot.dockRoot = WorkspaceNode::leaf(inventedId);
    snapshot.focusedTool = inventedId;
    snapshot.toolSettings.emplace(inventedId, ToolSettingsPayload{7, R"({"gigawatts":1.21})"});

    const auto normalized = WorkspaceNormalizer::normalize(snapshot, {{0, 0, 1280, 720}}, catalog);

    REQUIRE_FALSE(normalized.report.usedFallback);
    REQUIRE(normalized.snapshot.dockRoot.has_value());
    CHECK(normalized.snapshot.dockRoot->toolId == inventedId);
    CHECK(normalized.snapshot.focusedTool == inventedId);
    REQUIRE(normalized.snapshot.toolSettings.count(inventedId) == 1);
    CHECK(normalized.snapshot.toolSettings.at(inventedId).data == R"({"gigawatts":1.21})");

    // ...and the plan the shell would execute places it, with its settings.
    const auto bindings = std::vector<WorkspaceSnapshotApply::ToolBinding>{
        {inventedId, WorkspaceLayoutState::Tool{0}}};
    const auto plan = WorkspaceSnapshotApply::plan(normalized.snapshot, catalog, bindings);

    REQUIRE(plan.has_value());
    REQUIRE(plan->tools.size() == 1);
    CHECK(plan->tools.front().id == inventedId);
    CHECK(plan->tools.front().presentation == WorkspaceToolState::Presentation::docked);
    REQUIRE(plan->tools.front().settings.has_value());
    CHECK(plan->tools.front().settings->version == 7);
    CHECK(plan->focusedTool == inventedId);
}

TEST_CASE("an unknown tool's historical alias resolves too", "[tools][registration]")
{
    const auto catalog = catalogWithInventedTool();

    WorkspaceSnapshot snapshot;
    snapshot.dockRoot = WorkspaceNode::leaf(inventedAlias);
    snapshot.focusedTool = inventedAlias;

    const auto normalized = WorkspaceNormalizer::normalize(snapshot, {{0, 0, 1280, 720}}, catalog);

    REQUIRE(normalized.snapshot.dockRoot.has_value());
    CHECK(normalized.snapshot.dockRoot->toolId == inventedId);
    CHECK(normalized.snapshot.focusedTool == inventedId);
}

TEST_CASE("an unknown tool's settings are dropped on a version change", "[tools][registration]")
{
    const auto catalog = catalogWithInventedTool();

    WorkspaceSnapshot snapshot;
    snapshot.dockRoot = WorkspaceNode::leaf(inventedId);
    // The tool declares settingsVersion 7; this payload is from another build.
    snapshot.toolSettings.emplace(inventedId, ToolSettingsPayload{6, "stale"});

    const auto normalized = WorkspaceNormalizer::normalize(snapshot, {{0, 0, 1280, 720}}, catalog);

    // The tool still restores -- it just starts from its defaults.
    REQUIRE(normalized.snapshot.dockRoot.has_value());
    CHECK(normalized.snapshot.dockRoot->toolId == inventedId);
    CHECK(normalized.snapshot.toolSettings.empty());
}

TEST_CASE("a workspace round-trips a tool the shell has never heard of", "[tools][registration]")
{
    WorkspaceLayoutState layout;
    constexpr auto handle = WorkspaceLayoutState::Tool{0};
    layout.insert(handle, std::nullopt, WorkspaceLayoutState::DropZone::centre);

    const auto observations = std::vector<WorkspaceSnapshotCapture::ToolObservation>{
        {inventedId,
         handle,
         WorkspaceToolState::Presentation::docked,
         {},
         ToolSettingsPayload{7, "kept"}}};

    const auto captured = WorkspaceSnapshotCapture::capture(layout, observations, inventedId);

    REQUIRE(captured.dockRoot.has_value());
    CHECK(captured.dockRoot->toolId == inventedId);
    CHECK(captured.focusedTool == inventedId);
    REQUIRE(captured.toolSettings.count(inventedId) == 1);
    CHECK(captured.toolSettings.at(inventedId).data == "kept");
}
