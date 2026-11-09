#pragma once

#include "../../../../localisation/LocalisationRules.h"
#include "WorkspaceDocuments.h"

#include <string_view>
#include <vector>

class WorkspaceBuiltIns
{
  public:
    [[nodiscard]] static ToolSettingsPayload generalInstrumentTunerSettings()
    {
        return {
            1,
            R"({"displayMode":1,"easing":0.35,"averaging":5,"noteSwitchSemitones":0.55,"dropoutFrames":4,"graphDurationSeconds":20})"};
    }

    [[nodiscard]] static ToolSettingsPayload voiceTunerSettings()
    {
        return {
            1,
            R"({"displayMode":1,"easing":0.25,"averaging":7,"noteSwitchSemitones":0.45,"dropoutFrames":7,"graphDurationSeconds":30})"};
    }

    [[nodiscard]] static const NamedWorkspace& pitchPractice()
    {
        return all()[0];
    }

    [[nodiscard]] static const NamedWorkspace& vocalWarmUp()
    {
        return all()[1];
    }

    [[nodiscard]] static const NamedWorkspace& spectrumAnalysis()
    {
        return all()[2];
    }

    [[nodiscard]] static const NamedWorkspace& performancePreparation()
    {
        return all()[3];
    }

    [[nodiscard]] static const std::vector<NamedWorkspace>& all()
    {
        static const std::vector<NamedWorkspace> builtIns = []
        {
            WorkspaceSnapshot pitch;
            pitch.dockRoot = WorkspaceNode::leaf("tuner");
            pitch.focusedTool = "tuner";
            pitch.toolSettings.emplace("tuner", generalInstrumentTunerSettings());

            WorkspaceSnapshot vocal;
            vocal.dockRoot = WorkspaceNode::split(
                WorkspaceOrientation::horizontal, 0.5, WorkspaceNode::leaf("tuner"),
                WorkspaceNode::leaf("harmonic-analyzer"));
            vocal.focusedTool = "tuner";
            vocal.toolSettings.emplace("tuner", voiceTunerSettings());

            WorkspaceSnapshot spectrum;
            spectrum.dockRoot = WorkspaceNode::split(
                WorkspaceOrientation::horizontal, 0.58, WorkspaceNode::leaf("spectrogram"),
                WorkspaceNode::leaf("harmonic-analyzer"));
            spectrum.focusedTool = "spectrogram";

            WorkspaceSnapshot performance;
            performance.dockRoot = WorkspaceNode::split(
                WorkspaceOrientation::horizontal, 0.5, WorkspaceNode::leaf("tuner"),
                WorkspaceNode::tabGroup({"spectrogram", "harmonic-analyzer"}, "spectrogram"));
            performance.focusedTool = "tuner";
            performance.toolSettings.emplace("tuner", generalInstrumentTunerSettings());

            std::vector<NamedWorkspace> definitions;
            definitions.push_back({"pitch-practice", "Pitch Practice", std::move(pitch)});
            definitions.push_back({"vocal-warm-up", "Vocal Warm-up", std::move(vocal)});
            definitions.push_back({"spectrum-analysis", "Spectrum Analysis", std::move(spectrum)});
            definitions.push_back(
                {"performance-preparation", "Performance Preparation", std::move(performance)});
            return definitions;
        }();
        return builtIns;
    }

    // The catalogue identifier of a built-in workspace's name, as shown.
    // `name` stays in English: it is what a user's own workspace may not be
    // called, in a settings file read by a copy in any language.
    [[nodiscard]] static std::string_view nameKey(std::string_view id)
    {
        if (id == "pitch-practice")
        {
            return l10n::key("workspace.built-in.pitch-practice");
        }
        if (id == "vocal-warm-up")
        {
            return l10n::key("workspace.built-in.vocal-warm-up");
        }
        if (id == "spectrum-analysis")
        {
            return l10n::key("workspace.built-in.spectrum-analysis");
        }
        if (id == "performance-preparation")
        {
            return l10n::key("workspace.built-in.performance-preparation");
        }
        return {};
    }

    [[nodiscard]] static const NamedWorkspace* find(std::string_view id)
    {
        for (const auto& workspace : all())
        {
            if (workspace.id == id)
            {
                return &workspace;
            }
        }
        return nullptr;
    }
};