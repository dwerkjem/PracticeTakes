#pragma once

#include <cstddef>
#include <string>
#include <string_view>

// What an instance of a tool is called.
//
// Split out of MainComponent so the naming rules can be tested without a
// display (openspec/changes/multi-instance-tools). A user's label, when set,
// wins; otherwise the first instance keeps the tool's plain name -- which is
// what every existing workspace and screenshot expects -- and each later one
// carries its ordinal.
namespace ToolInstanceLabel
{
// The longest label kept. Long enough for "Violin, bowed, close mic", short
// enough to fit a tab.
inline constexpr std::size_t maximumCharacters = 40;

[[nodiscard]] inline std::string defaultName(std::string_view displayName, std::size_t ordinal)
{
    std::string name{displayName};
    if (ordinal > 1)
    {
        name += ' ';
        name += std::to_string(ordinal);
    }
    return name;
}

// A label as the user typed it, made safe to show and store: control characters
// removed, surrounding whitespace trimmed, and cut to maximumCharacters. Empty
// means "use the default name". Counting is by Unicode code point, so a cut
// never lands inside a multi-byte UTF-8 sequence.
[[nodiscard]] inline std::string sanitise(std::string_view label)
{
    std::string kept;
    kept.reserve(label.size());
    for (const auto character : label)
    {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20 || byte == 0x7f)
        {
            continue;
        }
        kept += character;
    }

    const auto isSpace = [](char character) { return character == ' '; };
    std::size_t first = 0;
    while (first < kept.size() && isSpace(kept[first]))
    {
        ++first;
    }
    std::size_t last = kept.size();
    while (last > first && isSpace(kept[last - 1]))
    {
        --last;
    }
    kept = kept.substr(first, last - first);

    // Walk code points: a byte that is not a UTF-8 continuation byte starts one.
    std::size_t codePoints = 0;
    for (std::size_t index = 0; index < kept.size(); ++index)
    {
        const auto byte = static_cast<unsigned char>(kept[index]);
        if ((byte & 0xC0u) != 0x80u)
        {
            if (codePoints == maximumCharacters)
            {
                kept.resize(index);
                break;
            }
            ++codePoints;
        }
    }

    // Cutting can expose trailing spaces that sat inside the limit.
    while (!kept.empty() && isSpace(kept.back()))
    {
        kept.pop_back();
    }
    return kept;
}

// The name to show: the label when there is one, the default otherwise.
[[nodiscard]] inline std::string
displayName(std::string_view toolDisplayName, std::size_t ordinal, std::string_view label)
{
    return label.empty() ? defaultName(toolDisplayName, ordinal) : std::string{label};
}
} // namespace ToolInstanceLabel
