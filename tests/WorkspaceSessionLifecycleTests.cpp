#include <catch2/catch_test_macros.hpp>

#include "application/shell/ui/workspace/model/WorkspaceCatalogCodec.h"
#include "application/shell/ui/workspace/model/WorkspaceSessionLifecycle.h"

TEST_CASE(
    "first launch restores Pitch Practice before deferred services start",
    "[workspace][startup]")
{
    WorkspaceCatalog catalog;

    const auto restored =
        WorkspaceSessionLifecycle::restore(std::move(catalog), {{0, 0, 1920, 1080}});

    CHECK(restored.recovered);
    CHECK(restored.catalog.active == WorkspaceBuiltIns::pitchPractice().snapshot);
    CHECK_FALSE(restored.catalog.activeSource.has_value());
}

TEST_CASE("active workspace topology survives session capture and restart", "[workspace][restart]")
{
    WorkspaceSnapshot active;
    active.dockRoot = WorkspaceNode::split(
        WorkspaceOrientation::vertical, 0.67, WorkspaceNode::leaf("tuner"),
        WorkspaceNode::tabGroup({"spectrogram", "harmonic-analyzer"}, "harmonic-analyzer"));
    active.focusedTool = "harmonic-analyzer";

    WorkspaceCatalog saved;
    saved.activeSource = WorkspaceBuiltIns::performancePreparation().id;
    saved.named.push_back(
        {"saved-spectrum", "Saved Spectrum", WorkspaceBuiltIns::spectrumAnalysis().snapshot});
    WorkspaceSessionLifecycle::captureActive(saved, active);
    const auto encoded = WorkspaceCatalogCodec::encode(saved);
    auto decoded = WorkspaceCatalogCodec::decode(encoded, {});
    REQUIRE(decoded.catalog.has_value());
    const auto restored =
        WorkspaceSessionLifecycle::restore(std::move(*decoded.catalog), {{0, 0, 1920, 1080}});

    CHECK_FALSE(restored.recovered);
    CHECK(restored.catalog.active == active);
    CHECK(restored.catalog.activeSource == WorkspaceBuiltIns::performancePreparation().id);
    REQUIRE(restored.catalog.named.size() == 1);
    CHECK(restored.catalog.named.front().id == "saved-spectrum");
    REQUIRE(restored.catalog.active.dockRoot.has_value());
    CHECK(restored.catalog.active.dockRoot->splitRatio == 0.67);
    REQUIRE(restored.catalog.active.dockRoot->second != nullptr);
    CHECK(restored.catalog.active.dockRoot->second->activeTab == "harmonic-analyzer");
}

TEST_CASE("restart constrains floating windows to connected displays", "[workspace][restart]")
{
    WorkspaceCatalog saved;
    saved.active.dockRoot = WorkspaceNode::leaf("tuner");
    saved.active.floating.push_back({"spectrogram", {5000, 4000, 1400, 900}});
    saved.active.focusedTool = "spectrogram";

    const auto restored = WorkspaceSessionLifecycle::restore(std::move(saved), {{0, 0, 1280, 720}});

    REQUIRE(restored.catalog.active.floating.size() == 1);
    CHECK(restored.catalog.active.floating.front().bounds == WorkspaceBounds{0, 0, 1280, 720});
    CHECK(restored.recovered);
}

TEST_CASE("floating bounds survive session capture and restart", "[workspace][restart]")
{
    WorkspaceCatalog saved;
    WorkspaceSnapshot active;
    active.dockRoot = WorkspaceNode::tabGroup({"spectrogram", "harmonic-analyzer"}, "spectrogram");
    active.floating.push_back({"tuner", {1200, 90, 600, 760}});
    active.focusedTool = "tuner";
    WorkspaceSessionLifecycle::captureActive(saved, active);

    const auto encoded = WorkspaceCatalogCodec::encode(saved);
    auto decoded = WorkspaceCatalogCodec::decode(encoded, {});
    REQUIRE(decoded.catalog.has_value());
    const auto restored =
        WorkspaceSessionLifecycle::restore(std::move(*decoded.catalog), {{0, 0, 1920, 1080}});

    CHECK_FALSE(restored.recovered);
    CHECK(restored.catalog.active == active);
    REQUIRE(restored.catalog.active.floating.size() == 1);
    CHECK(restored.catalog.active.floating.front().bounds == WorkspaceBounds{1200, 90, 600, 760});
}

TEST_CASE(
    "restart omits missing tools while preserving valid named workspaces",
    "[workspace][restart]")
{
    WorkspaceCatalog saved;
    saved.active.dockRoot = WorkspaceNode::split(
        WorkspaceOrientation::horizontal, 0.4, WorkspaceNode::leaf("missing-tool"),
        WorkspaceNode::leaf("tuner"));
    saved.active.focusedTool = "missing-tool";
    saved.named.push_back(
        {"saved-spectrum", "Saved Spectrum", WorkspaceBuiltIns::spectrumAnalysis().snapshot});

    const auto restored =
        WorkspaceSessionLifecycle::restore(std::move(saved), {{0, 0, 1920, 1080}});

    REQUIRE(restored.catalog.active.dockRoot.has_value());
    CHECK(restored.catalog.active.dockRoot->kind == WorkspaceNodeKind::leaf);
    CHECK(restored.catalog.active.dockRoot->toolId == "tuner");
    CHECK(restored.catalog.active.focusedTool == "tuner");
    REQUIRE(restored.catalog.named.size() == 1);
    CHECK(restored.catalog.named.front().id == "saved-spectrum");
    CHECK(restored.recovered);
}

TEST_CASE("reset replaces only the active workspace", "[workspace][reset]")
{
    WorkspaceCatalog catalog;
    catalog.active = WorkspaceBuiltIns::performancePreparation().snapshot;
    catalog.activeSource = WorkspaceBuiltIns::performancePreparation().id;
    catalog.named.push_back(
        {"saved-spectrum", "Saved Spectrum", WorkspaceBuiltIns::spectrumAnalysis().snapshot});
    const auto namedBefore = catalog.named;

    WorkspaceSessionLifecycle::resetActive(catalog);

    CHECK(catalog.active == WorkspaceBuiltIns::pitchPractice().snapshot);
    CHECK(catalog.activeSource == WorkspaceBuiltIns::pitchPractice().id);
    CHECK(catalog.named == namedBefore);
}
