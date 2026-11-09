#pragma once

#include "../localisation/Localisation.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
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
    // The plural entry that counts it: toolCountKey.
    std::string_view countKey;
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

// "8 Spectrograms, 7 Harmonic Analyzers and shared pitch detection": what is
// using the streams, largest first, skipping tools with nothing open.
[[nodiscard]] inline juce::String describeUsers(const Use& use)
{
    juce::StringArray parts;
    auto tools = use.tools;
    std::stable_sort(
        tools.begin(), tools.end(),
        [](const ToolCount& left, const ToolCount& right) { return left.open > right.open; });
    for (const auto& tool : tools)
    {
        if (tool.open > 0)
        {
            parts.add(l10n::plural(tool.countKey, static_cast<std::int64_t>(tool.open)));
        }
    }

    const auto other = use.other();
    if (other == 1)
    {
        parts.add(l10n::text("audio-streams.shared-pitch"));
    }
    else if (other > 1)
    {
        parts.add(l10n::plural("audio-streams.other", static_cast<std::int64_t>(other)));
    }

    if (parts.size() <= 1)
    {
        return parts.joinIntoString({});
    }
    const auto last = parts[parts.size() - 1];
    parts.remove(parts.size() - 1);
    return l10n::format(
        "list.last",
        {{"rest", parts.joinIntoString(l10n::text("list.separator"))}, {"last", last}});
}

// The whole explanation for an alert: how many streams there are, who has them,
// and what to do.
[[nodiscard]] inline juce::String explainFull(const Use& use)
{
    const auto users = describeUsers(use);
    const auto capacity = l10n::number(static_cast<double>(use.capacity), 0);
    return users.isEmpty()
               ? l10n::format("audio-streams.full", {{"capacity", capacity}})
               : l10n::format("audio-streams.full.by", {{"capacity", capacity}, {"users", users}});
}
} // namespace AudioStreamUse
