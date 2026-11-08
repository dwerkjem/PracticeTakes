#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

// Who is using AudioInputService's sample streams, said in a sentence.
//
// The shell knows which open tools read samples and how many slots the service
// has free; the service itself knows only pointers. What is left over after the
// tools are counted is attributed to the shared pitch detector, which holds one
// slot whatever is open. Pure, so the sentence a user sees when a stream runs
// out is tested without a display.
namespace AudioStreamUse
{
struct ToolCount
{
    std::string displayName;
    std::size_t open = 0;
};

struct Use
{
    std::size_t capacity = 0;
    std::size_t free = 0;
    std::vector<ToolCount> tools;

    [[nodiscard]] std::size_t used() const noexcept
    {
        return capacity >= free ? capacity - free : 0;
    }

    // Slots in use that no open tool accounts for.
    [[nodiscard]] std::size_t other() const noexcept
    {
        std::size_t byTools = 0;
        for (const auto& tool : tools)
        {
            byTools += tool.open;
        }
        return used() > byTools ? used() - byTools : 0;
    }
};

// "1 Spectrogram", "3 Harmonic Analyzers". Display names are plain nouns, so an
// English plural is the name with an "s".
[[nodiscard]] inline std::string counted(const ToolCount& tool)
{
    return std::to_string(tool.open) + " " + tool.displayName + (tool.open == 1 ? "" : "s");
}

// "8 Spectrograms, 7 Harmonic Analyzers and shared pitch detection": what is
// using the streams, largest first, skipping tools with nothing open.
[[nodiscard]] inline std::string describeUsers(const Use& use)
{
    std::vector<std::string> parts;
    auto tools = use.tools;
    std::stable_sort(
        tools.begin(), tools.end(),
        [](const ToolCount& left, const ToolCount& right) { return left.open > right.open; });
    for (const auto& tool : tools)
    {
        if (tool.open > 0)
        {
            parts.push_back(counted(tool));
        }
    }

    const auto other = use.other();
    if (other == 1)
    {
        parts.emplace_back("shared pitch detection");
    }
    else if (other > 1)
    {
        parts.push_back(std::to_string(other) + " other listeners");
    }

    std::string sentence;
    for (std::size_t index = 0; index < parts.size(); ++index)
    {
        if (index > 0)
        {
            sentence += index + 1 == parts.size() ? " and " : ", ";
        }
        sentence += parts[index];
    }
    return sentence;
}

// The whole explanation for an alert: how many streams there are, who has them,
// and what to do.
[[nodiscard]] inline std::string explainFull(const Use& use)
{
    std::string text = "All " + std::to_string(use.capacity) + " audio streams are in use";
    const auto users = describeUsers(use);
    if (!users.empty())
    {
        text += ", by " + users;
    }
    text += ". Close one of them to free a stream. Tuners don't use one.";
    return text;
}
} // namespace AudioStreamUse
