#include <catch2/catch_test_macros.hpp>

#include "application/shell/ui/workspace/model/WorkspaceDocuments.h"

#include <string>

TEST_CASE(
    "workspace snapshots are copyable and derive canonical tool presentation",
    "[workspace][documents]")
{
    WorkspaceNode tuner = WorkspaceNode::leaf("tuner");
    WorkspaceNode analysis =
        WorkspaceNode::tabGroup({"spectrogram", "harmonic-analyzer"}, "harmonic-analyzer");

    WorkspaceSnapshot original;
    original.dockRoot = WorkspaceNode::split(
        WorkspaceOrientation::horizontal, 0.58, std::move(tuner), std::move(analysis));
    original.floating.push_back({"settings", {120, 80, 640, 480}});
    original.focusedTool = "harmonic-analyzer";
    original.toolSettings.emplace("tuner", ToolSettingsPayload{1, "{\"displayMode\":2}"});

    const auto copy = original;

    REQUIRE(copy.dockRoot.has_value());
    CHECK(copy.dockRoot->kind == WorkspaceNodeKind::split);
    CHECK(copy.dockRoot->splitRatio == 0.58);
    REQUIRE(copy.dockRoot->second != nullptr);
    CHECK(copy.dockRoot->second->tabs.size() == 2);
    CHECK(copy.dockRoot->second->activeTab == "harmonic-analyzer");
    CHECK(copy.focusedTool == "harmonic-analyzer");
    CHECK(copy.toolSettings.at("tuner").version == 1);
    CHECK(copy.presentationOf("tuner") == WorkspaceToolPresentation::docked);
    CHECK(copy.presentationOf("settings") == WorkspaceToolPresentation::floating);
    CHECK(copy.presentationOf("missing") == WorkspaceToolPresentation::closed);
}

TEST_CASE(
    "workspace catalogs keep stable named identifiers independent of display names",
    "[workspace][documents]")
{
    WorkspaceCatalog catalog;
    catalog.active.dockRoot = WorkspaceNode::leaf("tuner");
    catalog.named.push_back({"workspace-7f5d", "Pitch Study", catalog.active});

    const auto copy = catalog;
    REQUIRE(copy.named.size() == 1);
    CHECK(copy.named.front().id == "workspace-7f5d");
    CHECK(copy.named.front().name == "Pitch Study");
    REQUIRE(copy.named.front().snapshot.dockRoot.has_value());
    CHECK(copy.named.front().snapshot.dockRoot->toolId == "tuner");
}