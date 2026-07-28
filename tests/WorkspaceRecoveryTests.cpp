#include <catch2/catch_test_macros.hpp>

#include "application/shell/ui/workspace/model/WorkspaceNormalizer.h"

#include <limits>

TEST_CASE(
    "workspace recovery resolves aliases removes duplicates and repairs structure",
    "[workspace][recovery]")
{
    WorkspaceSnapshot snapshot;
    snapshot.dockRoot = WorkspaceNode::split(
        WorkspaceOrientation::horizontal, std::numeric_limits<double>::infinity(),
        WorkspaceNode::tabGroup(
            {"harmonics", "missing-tool", "tuner", "harmonic-analyzer"}, "missing-tool"),
        WorkspaceNode::leaf("tuner"));
    snapshot.floating = {
        {"harmonics", {20, 20, 600, 400}},
        {"spectrogram", {4000, 300, 800, 700}},
        {"missing-tool", {0, 0, 100, 100}},
    };
    snapshot.focusedTool = "missing-tool";

    const auto result = WorkspaceNormalizer::normalize(snapshot, {{0, 0, 1920, 1040}});

    REQUIRE(result.snapshot.dockRoot.has_value());
    CHECK(result.snapshot.dockRoot->kind == WorkspaceNodeKind::tabs);
    CHECK(result.snapshot.dockRoot->tabs == std::vector<std::string>{"harmonic-analyzer", "tuner"});
    CHECK(result.snapshot.dockRoot->activeTab == "harmonic-analyzer");
    REQUIRE(result.snapshot.floating.size() == 1);
    CHECK(result.snapshot.floating.front().toolId == "spectrogram");
    CHECK(result.snapshot.floating.front().bounds == WorkspaceBounds{1120, 300, 800, 700});
    CHECK(result.snapshot.focusedTool == "harmonic-analyzer");
    CHECK_FALSE(result.report.usedFallback);
}

TEST_CASE(
    "workspace recovery constrains ratios and collapses malformed branches",
    "[workspace][recovery]")
{
    WorkspaceNode malformed;
    malformed.kind = WorkspaceNodeKind::split;
    malformed.orientation = WorkspaceOrientation::vertical;
    malformed.splitRatio = -3.0;
    malformed.first = std::make_unique<WorkspaceNode>(WorkspaceNode::leaf("tuner"));

    WorkspaceSnapshot collapsed;
    collapsed.dockRoot = std::move(malformed);
    auto collapsedResult = WorkspaceNormalizer::normalize(collapsed, {{0, 0, 1280, 720}});
    REQUIRE(collapsedResult.snapshot.dockRoot.has_value());
    CHECK(collapsedResult.snapshot.dockRoot->kind == WorkspaceNodeKind::leaf);
    CHECK(collapsedResult.snapshot.dockRoot->toolId == "tuner");

    WorkspaceSnapshot clamped;
    clamped.dockRoot = WorkspaceNode::split(
        WorkspaceOrientation::horizontal, 4.0, WorkspaceNode::leaf("tuner"),
        WorkspaceNode::leaf("spectrogram"));
    auto clampedResult = WorkspaceNormalizer::normalize(clamped, {{0, 0, 1280, 720}});
    REQUIRE(clampedResult.snapshot.dockRoot.has_value());
    CHECK(clampedResult.snapshot.dockRoot->splitRatio == 0.9);
}

TEST_CASE(
    "workspace recovery reports bounded input and falls back from an empty layout",
    "[workspace][recovery]")
{
    WorkspaceNode root = WorkspaceNode::leaf("missing-tool");
    for (std::size_t depth = 0; depth <= WorkspaceNormalizer::maximumDockDepth; ++depth)
    {
        root = WorkspaceNode::split(
            WorkspaceOrientation::horizontal, 0.5, std::move(root),
            WorkspaceNode::leaf("missing-tool"));
    }

    WorkspaceSnapshot snapshot;
    snapshot.dockRoot = std::move(root);
    snapshot.floating.resize(WorkspaceNormalizer::maximumFloatingTools + 1);

    const auto result = WorkspaceNormalizer::normalize(snapshot, {});

    CHECK(result.report.exceededDepthLimit);
    CHECK(result.report.exceededCollectionLimit);
    CHECK(result.report.usedFallback);
    REQUIRE(result.snapshot.dockRoot.has_value());
    CHECK(result.snapshot.dockRoot->kind == WorkspaceNodeKind::leaf);
    CHECK(result.snapshot.dockRoot->toolId == "tuner");
    CHECK(result.snapshot.focusedTool == "tuner");
}