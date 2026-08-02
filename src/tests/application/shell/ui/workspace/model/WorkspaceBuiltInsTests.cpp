#include <catch2/catch_test_macros.hpp>

#include "application/shell/ui/workspace/model/WorkspaceBuiltIns.h"

TEST_CASE("Pitch Practice is the immutable tuner fallback", "[workspace][built-ins]")
{
    const auto& workspace = WorkspaceBuiltIns::pitchPractice();

    CHECK(workspace.id == "pitch-practice");
    CHECK(workspace.name == "Pitch Practice");
    REQUIRE(workspace.snapshot.dockRoot.has_value());
    CHECK(workspace.snapshot.dockRoot->kind == WorkspaceNodeKind::leaf);
    CHECK(workspace.snapshot.dockRoot->toolId == "tuner");
    CHECK(workspace.snapshot.focusedTool == "tuner");
    CHECK(
        workspace.snapshot.toolSettings.at("tuner") ==
        WorkspaceBuiltIns::generalInstrumentTunerSettings());
}

TEST_CASE("Vocal Warm-up is a balanced voice-analysis split", "[workspace][built-ins]")
{
    const auto& workspace = WorkspaceBuiltIns::vocalWarmUp();

    CHECK(workspace.id == "vocal-warm-up");
    CHECK(workspace.name == "Vocal Warm-up");
    REQUIRE(workspace.snapshot.dockRoot.has_value());
    const auto& root = *workspace.snapshot.dockRoot;
    CHECK(root.kind == WorkspaceNodeKind::split);
    CHECK(root.orientation == WorkspaceOrientation::horizontal);
    CHECK(root.splitRatio == 0.5);
    REQUIRE(root.first != nullptr);
    REQUIRE(root.second != nullptr);
    CHECK(root.first->toolId == "tuner");
    CHECK(root.second->toolId == "harmonic-analyzer");
    CHECK(workspace.snapshot.toolSettings.at("tuner") == WorkspaceBuiltIns::voiceTunerSettings());
}

TEST_CASE("Spectrum Analysis gives the spectrogram the larger pane", "[workspace][built-ins]")
{
    const auto& workspace = WorkspaceBuiltIns::spectrumAnalysis();

    CHECK(workspace.id == "spectrum-analysis");
    CHECK(workspace.name == "Spectrum Analysis");
    REQUIRE(workspace.snapshot.dockRoot.has_value());
    const auto& root = *workspace.snapshot.dockRoot;
    CHECK(root.kind == WorkspaceNodeKind::split);
    CHECK(root.orientation == WorkspaceOrientation::horizontal);
    CHECK(root.splitRatio == 0.58);
    REQUIRE(root.first != nullptr);
    REQUIRE(root.second != nullptr);
    CHECK(root.first->toolId == "spectrogram");
    CHECK(root.second->toolId == "harmonic-analyzer");
}

TEST_CASE("Performance Preparation restores its selected analysis tab", "[workspace][built-ins]")
{
    const auto& workspace = WorkspaceBuiltIns::performancePreparation();

    CHECK(workspace.id == "performance-preparation");
    CHECK(workspace.name == "Performance Preparation");
    REQUIRE(workspace.snapshot.dockRoot.has_value());
    const auto& root = *workspace.snapshot.dockRoot;
    CHECK(root.kind == WorkspaceNodeKind::split);
    CHECK(root.orientation == WorkspaceOrientation::horizontal);
    CHECK(root.splitRatio == 0.5);
    REQUIRE(root.first != nullptr);
    REQUIRE(root.second != nullptr);
    CHECK(root.first->toolId == "tuner");
    CHECK(root.second->kind == WorkspaceNodeKind::tabs);
    CHECK(root.second->tabs == std::vector<std::string>{"spectrogram", "harmonic-analyzer"});
    CHECK(root.second->activeTab == "spectrogram");
    CHECK(
        workspace.snapshot.toolSettings.at("tuner") ==
        WorkspaceBuiltIns::generalInstrumentTunerSettings());
}

TEST_CASE("the built-in catalog contains each immutable workspace once", "[workspace][built-ins]")
{
    const auto& all = WorkspaceBuiltIns::all();

    REQUIRE(all.size() == 4);
    CHECK(all[0].id == "pitch-practice");
    CHECK(all[1].id == "vocal-warm-up");
    CHECK(all[2].id == "spectrum-analysis");
    CHECK(all[3].id == "performance-preparation");
    CHECK(WorkspaceBuiltIns::find("pitch-practice") == &all[0]);
    CHECK(WorkspaceBuiltIns::find("missing") == nullptr);
}