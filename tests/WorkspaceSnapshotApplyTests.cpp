#include <catch2/catch_test_macros.hpp>

#include "application/shell/ui/workspace/model/WorkspaceSnapshotApply.h"

#include <array>
#include <string>
#include <vector>

namespace
{
[[nodiscard]] std::vector<WorkspaceSnapshotApply::ToolBinding> toolBindings()
{
    return {
        {"tuner", WorkspaceLayoutState::Tool{0}},
        {"spectrogram", WorkspaceLayoutState::Tool{1}},
        {"harmonic-analyzer", WorkspaceLayoutState::Tool{2}},
    };
}
} // namespace

TEST_CASE(
    "workspace apply plan places each tool once and carries only workspace state",
    "[workspace][apply]")
{
    WorkspaceSnapshot snapshot;
    snapshot.dockRoot = WorkspaceNode::split(
        WorkspaceOrientation::horizontal, 0.61, WorkspaceNode::leaf("tuner"),
        WorkspaceNode::leaf("spectrogram"));
    snapshot.floating = {{"harmonic-analyzer", {1200, 80, 880, 640}}};
    snapshot.focusedTool = "harmonic-analyzer";
    snapshot.toolSettings.emplace("tuner", ToolSettingsPayload{1, R"({"displayMode":2})"});

    const WorkspaceToolRegistry registry;
    const auto plan = WorkspaceSnapshotApply::plan(snapshot, registry, toolBindings());

    REQUIRE(plan.has_value());
    REQUIRE(plan->layoutRoot != nullptr);
    CHECK(plan->layoutRoot->kind == WorkspaceLayoutState::NodeKind::split);
    CHECK(plan->layoutRoot->splitRatio == 0.61);
    REQUIRE(plan->tools.size() == 3);
    CHECK(plan->tools[0].id == "tuner");
    CHECK(plan->tools[0].presentation == WorkspaceToolState::Presentation::docked);
    CHECK(plan->tools[0].settings == snapshot.toolSettings.at("tuner"));
    CHECK(plan->tools[1].id == "spectrogram");
    CHECK(plan->tools[1].presentation == WorkspaceToolState::Presentation::docked);
    CHECK(plan->tools[2].id == "harmonic-analyzer");
    CHECK(plan->tools[2].presentation == WorkspaceToolState::Presentation::floating);
    CHECK(plan->tools[2].floatingBounds == WorkspaceBounds{1200, 80, 880, 640});
    CHECK(plan->focusedTool == "harmonic-analyzer");
}

TEST_CASE("workspace apply plan rejects duplicate dock placement", "[workspace][apply]")
{
    WorkspaceSnapshot snapshot;
    snapshot.dockRoot = WorkspaceNode::tabGroup({"tuner", "tuner"}, "tuner");

    const WorkspaceToolRegistry registry;
    CHECK_FALSE(WorkspaceSnapshotApply::plan(snapshot, registry, toolBindings()).has_value());
}

TEST_CASE(
    "workspace apply plan rejects duplicate dock and floating placement",
    "[workspace][apply]")
{
    WorkspaceSnapshot snapshot;
    snapshot.dockRoot = WorkspaceNode::leaf("tuner");
    snapshot.floating = {{"tuner", {20, 30, 900, 700}}};

    const WorkspaceToolRegistry registry;
    CHECK_FALSE(WorkspaceSnapshotApply::plan(snapshot, registry, toolBindings()).has_value());
}

TEST_CASE(
    "workspace apply executes one coordinated presentation rebuild without changing globals",
    "[workspace][apply]")
{
    WorkspaceSnapshotApply::Plan plan;
    plan.layoutRoot = WorkspaceLayoutState::makeLeafNode(WorkspaceLayoutState::Tool{0});
    plan.tools = {
        {"tuner", WorkspaceLayoutState::Tool{0}, WorkspaceToolState::Presentation::docked},
        {"spectrogram", WorkspaceLayoutState::Tool{1}, WorkspaceToolState::Presentation::closed},
        {"harmonic-analyzer",
         WorkspaceLayoutState::Tool{2},
         WorkspaceToolState::Presentation::floating,
         {1200, 80, 880, 640}},
    };
    plan.focusedTool = "harmonic-analyzer";

    struct GlobalPreferences
    {
        int theme = 2;
        bool microphoneMuted = true;
        double inputGain = 1.35;
        int audioDevice = 42;
        int fullscreenMode = 1;
        bool feedbackInvitationsDisabled = true;

        bool operator==(const GlobalPreferences&) const = default;
    };

    const GlobalPreferences expectedGlobals;
    auto globals = expectedGlobals;
    bool automaticSaveEnabled = true;
    int detachCount = 0;
    int replaceCount = 0;
    int rebuildCount = 0;
    std::array<int, 3> prepareCounts{};
    std::array<int, 3> createCounts{};
    std::array<int, 3> presentationCounts{};
    std::array<bool, 3> componentExists{};
    std::vector<std::string> operations;

    const auto toolIndex = [](WorkspaceLayoutState::Tool tool)
    { return static_cast<std::size_t>(tool); };
    const auto checkSuppressed = [&automaticSaveEnabled]() { CHECK_FALSE(automaticSaveEnabled); };

    const auto applied = WorkspaceSnapshotApply::execute(
        std::move(plan), automaticSaveEnabled,
        WorkspaceSnapshotApply::Callbacks{
            [&]
            {
                checkSuppressed();
                ++detachCount;
                operations.emplace_back("detach");
            },
            [&](std::unique_ptr<WorkspaceLayoutState::Node> root)
            {
                checkSuppressed();
                REQUIRE(root != nullptr);
                ++replaceCount;
                operations.emplace_back("replace");
            },
            [&](const WorkspaceSnapshotApply::ToolDirective& directive)
            {
                checkSuppressed();
                const auto index = toolIndex(directive.tool);
                ++prepareCounts[index];
                if (directive.presentation != WorkspaceToolState::Presentation::closed &&
                    !componentExists[index])
                {
                    componentExists[index] = true;
                    ++createCounts[index];
                }
                operations.push_back("prepare:" + directive.id);
            },
            [&](const WorkspaceSnapshotApply::ToolDirective& directive)
            {
                checkSuppressed();
                if (directive.presentation != WorkspaceToolState::Presentation::closed)
                {
                    ++presentationCounts[toolIndex(directive.tool)];
                }
                operations.push_back("present:" + directive.id);
            },
            [&]
            {
                checkSuppressed();
                ++rebuildCount;
                operations.emplace_back("rebuild");
            },
            [&](const std::optional<std::string>& focusedTool)
            {
                checkSuppressed();
                REQUIRE(focusedTool.has_value());
                operations.push_back("focus:" + *focusedTool);
            },
        });

    CHECK(applied);
    CHECK(automaticSaveEnabled);
    CHECK(detachCount == 1);
    CHECK(replaceCount == 1);
    CHECK(rebuildCount == 1);
    CHECK(prepareCounts == std::array<int, 3>{1, 1, 1});
    CHECK(createCounts == std::array<int, 3>{1, 0, 1});
    CHECK(presentationCounts == std::array<int, 3>{1, 0, 1});
    CHECK(globals == expectedGlobals);
    CHECK(
        operations == std::vector<std::string>{
                          "detach", "replace", "prepare:tuner", "prepare:spectrogram",
                          "prepare:harmonic-analyzer", "present:tuner", "present:spectrogram",
                          "present:harmonic-analyzer", "rebuild", "focus:harmonic-analyzer"});
}
