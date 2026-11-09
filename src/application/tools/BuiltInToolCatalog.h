#pragma once

#include "../localisation/LocalisationRules.h"
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
            // input_level::settingsVersion: the orientation. A test pins it.
            1,
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

// What a tool is called on screen, by catalogue identifier. `displayName`
// above stays the English name, which the capture harness's manifest and the
// feedback message use.
[[nodiscard]] inline std::string_view toolNameKey(std::string_view toolId) noexcept
{
    if (toolId == "tuner")
    {
        return l10n::key("tool.tuner");
    }
    if (toolId == "spectrogram")
    {
        return l10n::key("tool.spectrogram");
    }
    if (toolId == "harmonic-analyzer")
    {
        return l10n::key("tool.harmonic-analyzer");
    }
    if (toolId == "score-view")
    {
        return l10n::key("tool.score-view");
    }
    if (toolId == "note-view")
    {
        return l10n::key("tool.note-view");
    }
    if (toolId == "now-next")
    {
        return l10n::key("tool.now-next");
    }
    if (toolId == "input-level")
    {
        return l10n::key("tool.input-level");
    }
    if (toolId == "take-review")
    {
        return l10n::key("tool.take-review");
    }
    return {};
}

// "1 Spectrogram", "3 Spectrograms": a counted tool, as a plural entry.
[[nodiscard]] inline std::string_view toolCountKey(std::string_view toolId) noexcept
{
    if (toolId == "spectrogram")
    {
        return l10n::pluralKey("tool.spectrogram.count");
    }
    if (toolId == "harmonic-analyzer")
    {
        return l10n::pluralKey("tool.harmonic-analyzer.count");
    }
    if (toolId == "input-level")
    {
        return l10n::pluralKey("tool.input-level.count");
    }
    return l10n::pluralKey("tool.other.count");
}
