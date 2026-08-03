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
            "tuner", "Tuner", {"pitch-detector"}, ToolInstancePolicy::single, 1, {920, 760}},
        ToolDefinition{
            "spectrogram",
            "Spectrogram",
            {"spectrum"},
            ToolInstancePolicy::single,
            std::nullopt,
            {980, 650}},
        ToolDefinition{
            "harmonic-analyzer",
            "Harmonic Analyzer",
            {"harmonics", "harmonicAnalyzer"},
            ToolInstancePolicy::single,
            std::nullopt,
            {980, 700}},
    });
    return catalog;
}
