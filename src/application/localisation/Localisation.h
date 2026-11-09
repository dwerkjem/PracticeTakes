#pragma once

#include <juce_core/juce_core.h>

#include "LocalisationRules.h"

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// User-facing text, addressed by identifier (openspec/changes/internationalization).
//
// Every string the application shows is an entry in a catalogue, looked up by
// a stable identifier such as "score.import.slur.unsupported" -- never by its
// English wording, which would orphan every translation of a sentence the
// moment its English was reworded, and would give the measure "bar" and the
// tuner's "bar" view one entry between them (decision 1). English is a
// catalogue like any other, and the one every other falls back to
// (decision 2). The catalogues live in tools/packaging/localisation/ and are
// embedded in the binary.
namespace l10n
{
// One language's entries. JUCE's LocalisedStrings format -- `"key" = "text"`
// lines, a `language:` line -- read once into a map, since every lookup is by
// identifier and some are made while painting.
class Catalogue
{
  public:
    [[nodiscard]] static Catalogue parse(const juce::String& text);

    // The pseudolocale: every entry of `english`, transformed
    // (pseudolocale::transform). Generated each time, so it is never stale.
    [[nodiscard]] static Catalogue pseudolocalised(const Catalogue& english);

    [[nodiscard]] const juce::String* find(std::string_view key) const;

    [[nodiscard]] const juce::String& languageName() const noexcept
    {
        return name;
    }

    [[nodiscard]] const std::unordered_map<std::string, juce::String>& entries() const noexcept
    {
        return values;
    }

  private:
    juce::String name;
    std::unordered_map<std::string, juce::String> values;
};

// One named argument for a placeholder: {"bar", "12"} fills {bar}.
using Argument = std::pair<std::string_view, juce::String>;
using Arguments = std::initializer_list<Argument>;

// English, and the language chosen over it.
class Localisation
{
  public:
    explicit Localisation(Catalogue english);

    // `code` ("de", "pt-BR") with its catalogue; null, or English itself,
    // means English. The code decides plurals and numbers even where the
    // catalogue falls back.
    void choose(std::string_view code, const Catalogue* catalogue);

    [[nodiscard]] const std::string& languageCode() const noexcept
    {
        return code;
    }

    // The chosen language's text, else English's, else the identifier itself
    // -- visibly wrong rather than blank. The check in CI makes the last one a
    // failed build before it can reach anybody.
    [[nodiscard]] juce::String text(std::string_view key) const;

    // The English entry, whatever language is chosen: what a layout sized for
    // English measures a translation against.
    [[nodiscard]] juce::String englishText(std::string_view key) const;

    // text(key) with each {name} filled by name, in whatever order the
    // language puts them.
    [[nodiscard]] juce::String format(std::string_view key, Arguments arguments) const;

    // The same, with arguments a JUCE-free layer kept as data: a diagnostic's.
    [[nodiscard]] juce::String
    formatArguments(std::string_view key, const NamedArguments& arguments) const;

    // The form of `key` for `count`: the entry "key.one", "key.other" (and
    // "key.few" and so on, for a language that has them) that the language's
    // plural rule picks, falling back to the language's own "key.other" and
    // only then to English's. {count} is filled with the count, written as the
    // language writes numbers.
    [[nodiscard]] juce::String
    plural(std::string_view key, std::int64_t count, Arguments arguments = {}) const;

    [[nodiscard]] juce::String pluralArguments(
        std::string_view key,
        std::int64_t count,
        const NamedArguments& arguments) const;

    // `value` with `decimals` places, as the language writes it: 440,0 in
    // German, 440.0 in English.
    [[nodiscard]] juce::String number(double value, int decimals) const;

  private:
    [[nodiscard]] const juce::String* lookup(std::string_view key) const;

    Catalogue english;
    std::optional<Catalogue> chosen;
    std::string code = "en";
};

// The application's: the embedded English catalogue, and whatever language
// setLanguage chose. Message thread.
[[nodiscard]] Localisation& current();

// A language the application carries a catalogue for: its code, and its name
// as its own speakers write it ("Deutsch"), from the catalogue's `language:`.
struct LanguageChoice
{
    std::string code;
    juce::String name;
};

// Every embedded language, English first.
[[nodiscard]] std::vector<LanguageChoice> availableLanguages();

// Chooses the embedded catalogue for `code`, or the pseudolocale for
// pseudolocale::code. An unknown code runs in English rather than failing (and
// returns false).
bool setLanguage(std::string_view code);

// The application's text, shortened for the call sites.
[[nodiscard]] inline juce::String text(std::string_view key)
{
    return current().text(key);
}

[[nodiscard]] inline juce::String englishText(std::string_view key)
{
    return current().englishText(key);
}

[[nodiscard]] inline juce::String format(std::string_view key, Arguments arguments)
{
    return current().format(key, arguments);
}

[[nodiscard]] inline juce::String
formatArguments(std::string_view key, const NamedArguments& arguments)
{
    return current().formatArguments(key, arguments);
}

[[nodiscard]] inline juce::String
plural(std::string_view key, std::int64_t count, Arguments arguments = {})
{
    return current().plural(key, count, arguments);
}

[[nodiscard]] inline juce::String
pluralArguments(std::string_view key, std::int64_t count, const NamedArguments& arguments)
{
    return current().pluralArguments(key, count, arguments);
}

[[nodiscard]] inline juce::String number(double value, int decimals)
{
    return current().number(value, decimals);
}
} // namespace l10n
