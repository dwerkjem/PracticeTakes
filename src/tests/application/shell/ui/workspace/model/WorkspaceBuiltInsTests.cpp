#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "application/shell/ui/workspace/model/WorkspaceBuiltIns.h"
#include "features/analysis/tuner/TunerSettingsCodec.h"

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
TEST_CASE("Built-in workspaces carry the practice presets' tuner values", "[workspace][built-ins]")
{
    // The shell stores tool settings as opaque payloads, so the built-ins spell
    // the presets out as JSON rather than reading AppDefaults::presets. Nothing
    // else ties the two: change a preset and its workspace would keep the old
    // numbers, applying one tuning from Settings and another from the
    // workspace menu.
    const auto check = [](const ToolSettingsPayload& payload, AppDefaults::Preset preset)
    {
        const auto decoded = TunerSettingsCodec::decode(payload);
        REQUIRE(decoded.has_value());

        const AppDefaults::TunerSettings expected = AppDefaults::tunerPreset(preset);
        CHECK(decoded->displayMode == expected.displayMode);
        CHECK(decoded->easing == Catch::Approx(expected.easing));
        CHECK(decoded->averaging == Catch::Approx(expected.averaging));
        CHECK(decoded->noteSwitchSemitones == Catch::Approx(expected.noteSwitchSemitones));
        CHECK(decoded->dropoutFrames == Catch::Approx(expected.dropoutFrames));
        CHECK(decoded->graphDurationSeconds == Catch::Approx(expected.graphDurationSeconds));
    };

    check(
        WorkspaceBuiltIns::generalInstrumentTunerSettings(),
        AppDefaults::Preset::generalInstrument);
    check(WorkspaceBuiltIns::voiceTunerSettings(), AppDefaults::Preset::voice);
}
