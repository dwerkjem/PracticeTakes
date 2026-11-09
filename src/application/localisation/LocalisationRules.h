#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The parts of localisation that are rules rather than text
// (openspec/changes/internationalization, decisions 3 and 6): named
// placeholders, plural categories, and how a number is written. No JUCE
// dependency, so each rule is tested on its own.
namespace l10n
{
// Marks an identifier kept in a table and looked up later, so the check in
// CI (tools/scripts/quality/check_localisation.py) finds it. Does nothing, and
// needs no JUCE, so a JUCE-free table can hold identifiers too.
[[nodiscard]] constexpr std::string_view key(std::string_view identifier) noexcept
{
    return identifier;
}

// The same, for an identifier that will be passed to plural().
[[nodiscard]] constexpr std::string_view pluralKey(std::string_view identifier) noexcept
{
    return identifier;
}

// CLDR plural categories. Only `one` and `other` are used by the first
// languages (English, German, French, Spanish, Italian, Portuguese); the rest
// are here so a language that needs them is an entry in pluralCategory and
// not a change at every call site.
enum class PluralCategory
{
    zero,
    one,
    two,
    few,
    many,
    other
};

[[nodiscard]] constexpr std::string_view pluralName(PluralCategory category) noexcept
{
    switch (category)
    {
    case PluralCategory::zero:
        return "zero";
    case PluralCategory::one:
        return "one";
    case PluralCategory::two:
        return "two";
    case PluralCategory::few:
        return "few";
    case PluralCategory::many:
        return "many";
    case PluralCategory::other:
        return "other";
    }
    return "other";
}

// The language part of a code: "pt-BR" and "pt_BR" are "pt".
[[nodiscard]] inline std::string_view languageOf(std::string_view code) noexcept
{
    const auto end = code.find_first_of("-_");
    return end == std::string_view::npos ? code : code.substr(0, end);
}

// Which language to run in: the one chosen in Settings when there is a
// catalogue for it; otherwise -- "Automatic", or a choice whose catalogue is
// gone -- the system's language when there is one for that; otherwise English.
// `available` holds language codes ("en", "de") with catalogues.
[[nodiscard]] inline std::string chooseLanguage(
    std::string_view chosen,
    std::string_view system,
    const std::vector<std::string>& available)
{
    const auto has = [&available](std::string_view code)
    {
        for (const std::string& candidate : available)
        {
            if (candidate == languageOf(code))
            {
                return true;
            }
        }
        return false;
    };
    if (!chosen.empty() && has(chosen))
    {
        return std::string(chosen);
    }
    if (!system.empty() && has(system))
    {
        return std::string(system);
    }
    return "en";
}

// Which form a whole-number count takes. French puts zero with one ("0
// mesure"); the others give one alone to one.
[[nodiscard]] inline PluralCategory
pluralCategory(std::string_view code, std::int64_t count) noexcept
{
    const std::string_view language = languageOf(code);
    const std::int64_t magnitude = count < 0 ? -count : count;
    if (language == "fr")
    {
        return magnitude <= 1 ? PluralCategory::one : PluralCategory::other;
    }
    return magnitude == 1 ? PluralCategory::one : PluralCategory::other;
}

using NamedArguments = std::vector<std::pair<std::string, std::string>>;

// `pattern` with each {name} replaced by its argument, wherever the pattern
// puts it: a translator reorders placeholders by name. A placeholder with no
// argument is left as written, visibly, rather than dropped.
[[nodiscard]] inline std::string
formatNamed(std::string_view pattern, const NamedArguments& arguments)
{
    std::string result;
    result.reserve(pattern.size());
    std::size_t at = 0;
    while (at < pattern.size())
    {
        const std::size_t open = pattern.find('{', at);
        if (open == std::string_view::npos)
        {
            result.append(pattern.substr(at));
            break;
        }
        const std::size_t close = pattern.find('}', open + 1);
        if (close == std::string_view::npos)
        {
            result.append(pattern.substr(at));
            break;
        }
        result.append(pattern.substr(at, open - at));
        const std::string_view name = pattern.substr(open + 1, close - open - 1);
        bool replaced = false;
        for (const auto& [key, value] : arguments)
        {
            if (key == name)
            {
                result.append(value);
                replaced = true;
                break;
            }
        }
        if (!replaced)
        {
            result.append(pattern.substr(open, close - open + 1));
        }
        at = close + 1;
    }
    return result;
}

// How a language writes a number (decision 6).
struct NumberStyle
{
    std::string decimal = ".";
    std::string group = ",";

    // Digits before the first group separator is used: Spanish writes 1000
    // and 10 000, so it groups only from five digits.
    int minimumGroupingDigits = 1;
};

[[nodiscard]] inline NumberStyle numberStyleFor(std::string_view code)
{
    const std::string_view language = languageOf(code);
    if (language == "de" || language == "it" || language == "pt")
    {
        return {",", ".", 1};
    }
    if (language == "es")
    {
        return {",", ".", 2};
    }
    if (language == "fr")
    {
        // A narrow no-break space, as French typesetting groups thousands.
        return {",", "\xE2\x80\xAF", 1};
    }
    return {};
}

// `value` with `decimals` places, in `style`. Rounded half away from zero.
[[nodiscard]] inline std::string formatNumber(double value, int decimals, const NumberStyle& style)
{
    if (!std::isfinite(value))
    {
        return std::isnan(value) ? "NaN" : (value < 0 ? "-\xE2\x88\x9E" : "\xE2\x88\x9E");
    }
    decimals = decimals < 0 ? 0 : decimals;
    const double scale = std::pow(10.0, decimals);
    const bool negative = value < 0.0;
    const double rounded = std::round(std::fabs(value) * scale);
    const auto whole = static_cast<std::uint64_t>(std::floor(rounded / scale));
    const auto fraction =
        static_cast<std::uint64_t>(rounded - (static_cast<double>(whole) * scale));

    std::string digits = std::to_string(whole);
    std::string grouped;
    const bool groups = static_cast<int>(digits.size()) > 3 + (style.minimumGroupingDigits - 1);
    for (std::size_t index = 0; index < digits.size(); ++index)
    {
        if (groups && index > 0 && (digits.size() - index) % 3 == 0)
        {
            grouped += style.group;
        }
        grouped += digits[index];
    }

    std::string result = (negative && (whole > 0 || fraction > 0)) ? "-" + grouped : grouped;
    if (decimals > 0)
    {
        std::string tail = std::to_string(fraction);
        tail.insert(0, static_cast<std::size_t>(decimals) - tail.size(), '0');
        result += style.decimal + tail;
    }
    return result;
}
} // namespace l10n
