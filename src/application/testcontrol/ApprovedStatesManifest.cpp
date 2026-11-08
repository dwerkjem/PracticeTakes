#include "ApprovedStatesManifest.h"

#include "../shell/ui/ShellWindowTitles.h"
#include "../tools/BuiltInToolCatalog.h"

#include <array>
#include <cstdio>
#include <string_view>
#include <vector>

namespace testcontrol
{
namespace
{
[[nodiscard]] std::string_view nameOf(ToolPresentation presentation)
{
    switch (presentation)
    {
    case ToolPresentation::none:
        return "none";
    case ToolPresentation::docked:
        return "docked";
    case ToolPresentation::floating:
        return "floating";
    }
    return "none";
}

[[nodiscard]] std::string_view nameOf(WorkspaceArrangement arrangement)
{
    switch (arrangement)
    {
    case WorkspaceArrangement::single:
        return "single";
    case WorkspaceArrangement::split:
        return "split";
    case WorkspaceArrangement::tabbed:
        return "tabbed";
    }
    return "single";
}

[[nodiscard]] std::string_view nameOf(WindowGeometry geometry)
{
    switch (geometry)
    {
    case WindowGeometry::normal:
        return "normal";
    case WindowGeometry::narrow:
        return "narrow";
    case WindowGeometry::fullscreen:
        return "fullscreen";
    }
    return "normal";
}

[[nodiscard]] std::string_view nameOf(MicrophoneCondition microphone)
{
    switch (microphone)
    {
    case MicrophoneCondition::available:
        return "available";
    case MicrophoneCondition::muted:
        return "muted";
    case MicrophoneCondition::unavailable:
        return "unavailable";
    }
    return "available";
}

// A JSON string literal. Every string here is ASCII written in this repository,
// but a quote or a backslash in a description must not break the file.
[[nodiscard]] std::string quoted(std::string_view text)
{
    std::string out = "\"";
    for (const char c : text)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                std::array<char, 8> escape{};
                std::snprintf(escape.data(), escape.size(), "\\u%04x", static_cast<unsigned>(c));
                out += escape.data();
            }
            else
            {
                out += c;
            }
        }
    }
    return out + "\"";
}

// Fifteen significant digits, which prints a tone as "440" and gives the same
// characters on every platform. A whole number gets no point, so the file does
// not change with a compiler's taste in trailing zeros.
[[nodiscard]] std::string number(double value)
{
    std::array<char, 32> text{};
    std::snprintf(text.data(), text.size(), "%.15g", value);
    return text.data();
}

[[nodiscard]] std::string boolean(bool value)
{
    return value ? "true" : "false";
}

[[nodiscard]] std::string stringArray(const std::vector<std::string>& values)
{
    std::string out = "[";
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        out += (i == 0 ? "" : ", ") + quoted(values[i]);
    }
    return out + "]";
}

// The canonical id of each tool a state names, which may be written as a
// historical alias -- "harmonics" for the harmonic analyzer.
[[nodiscard]] std::vector<std::string> toolIdsOf(const ApprovedWindowState& state)
{
    std::vector<std::string> ids;
    ids.reserve(state.tools.size());
    for (const std::string& tool : state.tools)
    {
        ids.push_back(builtInToolCatalog().resolve(tool).value_or(tool));
    }
    return ids;
}

void appendState(std::string& out, const ApprovedWindowState& state)
{
    const std::string indent = "      ";
    out += "    {\n";
    out += indent + "\"id\": " + quoted(state.id) + ",\n";
    out += indent + "\"description\": " + quoted(state.description) + ",\n";
    out += indent + "\"tools\": " + stringArray(state.tools) + ",\n";
    out += indent + "\"toolIds\": " + stringArray(toolIdsOf(state)) + ",\n";
    out += indent + "\"presentation\": " + quoted(nameOf(state.presentation)) + ",\n";
    out += indent + "\"arrangement\": " + quoted(nameOf(state.arrangement)) + ",\n";
    out += indent + "\"geometry\": " + quoted(nameOf(state.geometry)) + ",\n";
    out += indent + "\"microphone\": " + quoted(nameOf(state.microphone)) + ",\n";
    out += indent + "\"settingsOpen\": " + boolean(state.settingsOpen) + ",\n";
    out += indent + "\"settingsPanel\": " + quoted(state.settingsPanel) + ",\n";
    out += indent + "\"feedbackOpen\": " + boolean(state.feedbackOpen) + ",\n";
    out += indent + "\"toneHz\": " + number(state.toneHz) + ",\n";
    out += indent + "\"toolView\": " + quoted(state.toolView) + ",\n";
    out += indent + "\"scoreFixture\": " + quoted(state.scoreFixture) + ",\n";
    out += indent + "\"showScoreImportSummary\": " + boolean(state.showScoreImportSummary) + ",\n";
    out += indent + "\"holdScoreImportInProgress\": " + boolean(state.holdScoreImportInProgress) +
           ",\n";
    out += indent + "\"clockPlaying\": " + boolean(state.clockPlaying) + ",\n";
    out += indent + "\"clockBar\": " + std::to_string(state.clockBar) + ",\n";
    out += indent + "\"recordTake\": " + boolean(state.recordTake) + ",\n";
    out += indent + "\"reviewTake\": " + boolean(state.reviewTake) + ",\n";
    out += indent + "\"mixOpen\": " + boolean(state.mixOpen) + ",\n";
    out += indent + "\"referencePitchHz\": " + number(state.referencePitchHz) + ",\n";
    out += indent + "\"windowTitle\": " + quoted(windowTitleOf(state)) + "\n";
    out += "    }";
}
} // namespace

const std::string& approvedStatesManifestPath()
{
    static const std::string path{"docs/contracts/testcontrol/approved-states.json"};

    return path;
}

std::string windowTitleOf(const ApprovedWindowState& state)
{
    // The window on top is the one the state is about. The feedback form can
    // open over a tool, and Settings over the workspace, so they come first.
    if (state.feedbackOpen)
    {
        return std::string(shellwindows::feedback);
    }
    if (state.settingsOpen)
    {
        return std::string(shellwindows::settings);
    }
    if (!state.scoreFixture.empty() &&
        (state.showScoreImportSummary || state.holdScoreImportInProgress))
    {
        return std::string(shellwindows::scoreImport);
    }
    if (state.presentation == ToolPresentation::floating && !state.tools.empty())
    {
        // The first instance of a tool keeps its display name; only a second
        // one is numbered, and no state opens a second.
        if (const ToolDefinition* tool = builtInToolCatalog().find(
                builtInToolCatalog().resolve(state.tools.front()).value_or(state.tools.front())))
        {
            return tool->displayName;
        }
    }
    return {};
}

std::string approvedStatesManifestJson()
{
    std::string out = "{\n";
    out += "  \"generated\": " +
           quoted("By ApprovedStatesManifest from src/application/testcontrol/"
                  "ApprovedWindowStates.cpp. Do not edit: a local build of PracticeTakesTests "
                  "rewrites it.") +
           ",\n";
    out += "  \"themes\": " + stringArray(approvedThemeNames()) + ",\n";
    out += "  \"geometries\": " + stringArray(approvedGeometryNames()) + ",\n";

    out += "  \"clickTargets\": [\n";
    const auto& targets = approvedClickTargets();
    for (std::size_t i = 0; i < targets.size(); ++i)
    {
        out += "    {\"id\": " + quoted(targets[i].id) + ", \"description\": " +
               quoted(targets[i].description) + "}" + (i + 1 < targets.size() ? ",\n" : "\n");
    }
    out += "  ],\n";

    out += "  \"states\": [\n";
    const auto& states = approvedWindowStates();
    for (std::size_t i = 0; i < states.size(); ++i)
    {
        appendState(out, states[i]);
        out += i + 1 < states.size() ? ",\n" : "\n";
    }
    out += "  ]\n";
    return out + "}\n";
}
} // namespace testcontrol
