#pragma once

#include "ToolCatalog.h"

// The roster of tools the application ships.
//
// This is the single place a tool's identity is declared. The JUCE-facing half
// -- the factory that constructs the component and the codec that serialises
// its settings -- is attached in BuiltInTools.cpp, which builds its registry
// from this same catalog so the two halves cannot drift apart.
//
// Adding a tool means adding one entry here and one factory there.
[[nodiscard]] inline const ToolCatalog& builtInToolCatalog()
{
    static const ToolCatalog catalog({
        ToolDefinition{
            "tuner",
            "Tuner",
            // The Target Note tool's ids, permanently: its target is shown in
            // the tuner now (system-wide-target-note decision 5), and a
            // workspace that names it still opens.
            {"pitch-detector", "target-note", "score-target", "targetNote"},
            ToolInstancePolicy::multi,
            1,
            {920, 760},
            /* readsAudioSamples: pitch comes from SharedPitchAnalysis */ false},
        ToolDefinition{
            "spectrogram",
            "Spectrogram",
            {"spectrum"},
            ToolInstancePolicy::multi,
            std::nullopt,
            {980, 650},
            /* readsAudioSamples */ true},
        ToolDefinition{
            "harmonic-analyzer",
            "Harmonic Analyzer",
            {"harmonics", "harmonicAnalyzer"},
            ToolInstancePolicy::multi,
            std::nullopt,
            {980, 700},
            /* readsAudioSamples */ true},
        ToolDefinition{
            "score-view",
            "Score View",
            {"notation", "scoreView"},
            ToolInstancePolicy::single,
            // score_view::settingsVersion; a test pins the two together, since
            // this catalog may not include a feature's header.
            1,
            {1060, 620}},
        ToolDefinition{
            "note-view",
            "Note View",
            {},
            ToolInstancePolicy::multi,
            // NoteViewComponent::settingsVersion: the clef.
            1,
            {900, 480},
            /* readsAudioSamples: pitch comes from SharedPitchAnalysis */ false},
        ToolDefinition{
            "now-next",
            "Now & Next",
            {"performance-view"},
            ToolInstancePolicy::multi,
            // NowNextComponent::settingsVersion: look-ahead, contrast, part.
            1,
            {900, 460},
            /* readsAudioSamples: it reads the clock and the score only */ false},
        ToolDefinition{
            "input-level",
            "Input Level",
            {},
            ToolInstancePolicy::multi,
            std::nullopt,
            {720, 300},
            // Its own stream, measured as a take's attacks are (#381).
            /* readsAudioSamples */ true},
        ToolDefinition{
            "take-review",
            "Take Review",
            {"review"},
            ToolInstancePolicy::single,
            std::nullopt,
            {760, 520}},
    });
    return catalog;
}
