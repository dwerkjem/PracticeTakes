#include <catch2/catch_test_macros.hpp>

#include "application/shell/ui/workspace/model/WorkspaceSnapshotCapture.h"

#include <map>
#include <optional>
#include <string>

namespace
{
using Tool = WorkspaceLayoutState::Tool;
using Presentation = WorkspaceToolState::Presentation;

constexpr Tool tuner{0};
constexpr Tool spectrogram{1};
constexpr Tool harmonics{2};

WorkspaceSnapshotCapture::ToolObservation observe(
    std::string id,
    Tool tool,
    Presentation presentation,
    WorkspaceBounds bounds = {},
    std::optional<ToolSettingsPayload> settings = std::nullopt)
{
    return {std::move(id), tool, presentation, bounds, std::move(settings)};
}
} // namespace

TEST_CASE(
    "workspace capture records presentation floating bounds focus and tuner settings",
    "[workspace][capture]")
{
    WorkspaceLayoutState layout;
    auto root = WorkspaceLayoutState::makeSplitNode(
        WorkspaceLayoutState::Orientation::horizontal, 0.62,
        WorkspaceLayoutState::makeLeafNode(tuner), WorkspaceLayoutState::makeLeafNode(spectrogram));
    REQUIRE(layout.replaceRoot(std::move(root)));

    auto tunerSettings = ToolSettingsPayload{1, R"({"displayMode":2,"easing":0.2})"};
    auto tools = std::vector<WorkspaceSnapshotCapture::ToolObservation>{
        observe("tuner", tuner, Presentation::docked, {}, tunerSettings),
        observe("spectrogram", spectrogram, Presentation::docked),
        observe("harmonic-analyzer", harmonics, Presentation::floating, {1300, 80, 960, 680})};

    const auto captured = WorkspaceSnapshotCapture::capture(layout, tools, "harmonic-analyzer");

    REQUIRE(captured.dockRoot.has_value());
    CHECK(captured.dockRoot->kind == WorkspaceNodeKind::split);
    CHECK(captured.dockRoot->splitRatio == 0.62);
    REQUIRE(captured.dockRoot->first != nullptr);
    REQUIRE(captured.dockRoot->second != nullptr);
    CHECK(captured.dockRoot->first->toolId == "tuner");
    CHECK(captured.dockRoot->second->toolId == "spectrogram");
    CHECK(captured.presentationOf("tuner") == WorkspaceToolPresentation::docked);
    CHECK(captured.presentationOf("harmonic-analyzer") == WorkspaceToolPresentation::floating);
    REQUIRE(captured.floating.size() == 1);
    CHECK(captured.floating.front().bounds == WorkspaceBounds{1300, 80, 960, 680});
    CHECK(captured.focusedTool == "harmonic-analyzer");
    CHECK(captured.toolSettings.at("tuner") == tunerSettings);

    tools[0].presentation = Presentation::floating;
    tools[0].floatingBounds = {48, 64, 920, 760};
    tools[0].settings = ToolSettingsPayload{1, R"({"displayMode":1,"easing":0.4})"};
    tools[2].presentation = Presentation::closed;
    REQUIRE(layout.replaceRoot(WorkspaceLayoutState::makeLeafNode(spectrogram)));

    const auto recaptured = WorkspaceSnapshotCapture::capture(layout, tools, "tuner");

    CHECK(recaptured.presentationOf("tuner") == WorkspaceToolPresentation::floating);
    CHECK(recaptured.presentationOf("harmonic-analyzer") == WorkspaceToolPresentation::closed);
    REQUIRE(recaptured.floating.size() == 1);
    CHECK(recaptured.floating.front().bounds == WorkspaceBounds{48, 64, 920, 760});
    CHECK(recaptured.focusedTool == "tuner");
    CHECK(recaptured.toolSettings.at("tuner") == *tools[0].settings);
}