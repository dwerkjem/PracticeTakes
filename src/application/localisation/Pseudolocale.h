#pragma once

#include <cstddef>
#include <string>
#include <string_view>

// The pseudolocale (openspec/changes/archive/2026-11-09-internationalization, decision 7).
//
// Generated from the English catalogue, never written: every entry longer by
// about 40%, its letters accented, and bracketed -- "[Šéţţîñĝš ~~~~]" -- so a
// capture in it shows what a translation would break (text clipped, truncated,
// not wrapped) without a translator, and any text *not* bracketed is text the
// sweep missed. Placeholders and line breaks are kept as they are, so the entry
// still formats.
//
// JUCE-free and outside the l10n namespace, because the capture harness's
// manifest (application/testcontrol, which localises nothing) uses the same
// function to say what a window is called in the pseudolocale.
namespace pseudolocale
{
// The code a pseudolocale is chosen by: Windows' own, so nothing real has it.
inline constexpr std::string_view code = "qps-ploc";

namespace detail
{
// Each ASCII letter's accented stand-in, in UTF-8: still readable, and wide
// enough letters that a layout sized for English shows it.
[[nodiscard]] inline std::string_view accented(char letter) noexcept
{
    switch (letter)
    {
    case 'a':
        return "\xC3\xA1";
    case 'c':
        return "\xC3\xA7";
    case 'e':
        return "\xC3\xA9";
    case 'i':
        return "\xC3\xAE";
    case 'n':
        return "\xC3\xB1";
    case 'o':
        return "\xC3\xB6";
    case 'u':
        return "\xC3\xBC";
    case 'y':
        return "\xC3\xBD";
    case 'A':
        return "\xC3\x81";
    case 'C':
        return "\xC3\x87";
    case 'E':
        return "\xC3\x89";
    case 'I':
        return "\xC3\x8E";
    case 'N':
        return "\xC3\x91";
    case 'O':
        return "\xC3\x96";
    case 'U':
        return "\xC3\x9C";
    default:
        return {};
    }
}
} // namespace detail

// `english` in the pseudolocale.
[[nodiscard]] inline std::string transform(std::string_view english)
{
    std::string body;
    std::size_t visible = 0;
    bool inPlaceholder = false;
    for (std::size_t index = 0; index < english.size(); ++index)
    {
        const char character = english[index];
        if (character == '{')
        {
            inPlaceholder = true;
        }
        if (inPlaceholder)
        {
            body += character;
            inPlaceholder = character != '}';
            continue;
        }
        const auto byte = static_cast<unsigned char>(character);
        if ((byte & 0xC0u) != 0x80u)
        {
            ++visible;
        }
        const std::string_view replacement = detail::accented(character);
        if (replacement.empty())
        {
            body += character;
        }
        else
        {
            body.append(replacement);
        }
    }

    // About 40% longer, the expansion German and French typically need; at
    // least one, so a one-letter entry grows too.
    const std::size_t padding = (visible * 2 + 4) / 5;
    return "[" + body + " " + std::string(padding == 0 ? 1 : padding, '~') + "]";
}
} // namespace pseudolocale
