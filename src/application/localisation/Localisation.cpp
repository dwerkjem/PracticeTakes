#include "Localisation.h"

#include "Pseudolocale.h"

#include <PracticeTakesCatalogues.h>

#include <memory>

namespace l10n
{
Catalogue Catalogue::parse(const juce::String& text)
{
    // Parsed by JUCE, so the files are the format its tools already know;
    // copied out into a map, since its own lookup is a linear search.
    const juce::LocalisedStrings strings(text, false);
    Catalogue catalogue;
    catalogue.name = strings.getLanguageName();
    const auto& mappings = strings.getMappings();
    for (int index = 0; index < mappings.size(); ++index)
    {
        catalogue.values.emplace(
            mappings.getAllKeys()[index].toStdString(), mappings.getAllValues()[index]);
    }
    return catalogue;
}

Catalogue Catalogue::pseudolocalised(const Catalogue& english)
{
    Catalogue catalogue;
    catalogue.name = "Pseudolocale";
    for (const auto& [key, value] : english.values)
    {
        catalogue.values.emplace(
            key, juce::String::fromUTF8(pseudolocale::transform(value.toStdString()).c_str()));
    }
    return catalogue;
}

const juce::String* Catalogue::find(std::string_view key) const
{
    const auto found = values.find(std::string(key));
    return found != values.end() ? &found->second : nullptr;
}

Localisation::Localisation(Catalogue englishCatalogue) : english(std::move(englishCatalogue)) {}

void Localisation::choose(std::string_view languageCode, const Catalogue* catalogue)
{
    code = std::string(languageCode.empty() ? std::string_view("en") : languageCode);
    if (catalogue != nullptr && languageOf(code) != "en")
    {
        chosen = *catalogue;
    }
    else
    {
        chosen.reset();
    }
}

const juce::String* Localisation::lookup(std::string_view key) const
{
    if (chosen.has_value())
    {
        if (const auto* found = chosen->find(key))
        {
            return found;
        }
    }
    return english.find(key);
}

juce::String Localisation::text(std::string_view key) const
{
    if (const auto* found = lookup(key))
    {
        return *found;
    }
    return juce::String(std::string(key));
}

juce::String Localisation::englishText(std::string_view key) const
{
    if (const auto* found = english.find(key))
    {
        return *found;
    }
    return juce::String(std::string(key));
}

juce::String Localisation::format(std::string_view key, Arguments arguments) const
{
    NamedArguments named;
    named.reserve(arguments.size());
    for (const auto& [name, value] : arguments)
    {
        named.emplace_back(std::string(name), value.toStdString());
    }
    return juce::String::fromUTF8(formatNamed(text(key).toStdString(), named).c_str());
}

juce::String
Localisation::formatArguments(std::string_view key, const NamedArguments& arguments) const
{
    return juce::String::fromUTF8(formatNamed(text(key).toStdString(), arguments).c_str());
}

juce::String
Localisation::plural(std::string_view key, std::int64_t count, Arguments arguments) const
{
    NamedArguments named;
    named.reserve(arguments.size());
    for (const auto& [name, value] : arguments)
    {
        named.emplace_back(std::string(name), value.toStdString());
    }
    return pluralArguments(key, count, named);
}

juce::String Localisation::pluralArguments(
    std::string_view key,
    std::int64_t count,
    const NamedArguments& arguments) const
{
    const std::string base(key);
    const std::string form = base + "." + std::string(pluralName(pluralCategory(code, count)));
    const std::string other = base + ".other";

    // The chosen language's own forms before any of English's: a language
    // without a singular gives only "other", and English's "one" would put
    // an English sentence in the middle of it.
    const juce::String* pattern = nullptr;
    if (chosen.has_value())
    {
        pattern = chosen->find(form);
        pattern = pattern != nullptr ? pattern : chosen->find(other);
    }
    pattern = pattern != nullptr ? pattern : english.find(form);
    pattern = pattern != nullptr ? pattern : english.find(other);
    const juce::String chosenPattern = pattern != nullptr ? *pattern : juce::String(other);

    NamedArguments named;
    named.reserve(arguments.size() + 1);
    named.emplace_back("count", formatNumber(static_cast<double>(count), 0, numberStyleFor(code)));
    named.insert(named.end(), arguments.begin(), arguments.end());
    return juce::String::fromUTF8(formatNamed(chosenPattern.toStdString(), named).c_str());
}

juce::String Localisation::number(double value, int decimals) const
{
    return juce::String::fromUTF8(formatNumber(value, decimals, numberStyleFor(code)).c_str());
}

namespace
{
// The embedded catalogue for a language code, by its file name: "de" is
// de.txt, embedded as de_txt.
[[nodiscard]] std::optional<Catalogue> embedded(std::string_view code)
{
    const std::string resource = std::string(languageOf(code)) + "_txt";
    int size = 0;
    const char* data = PracticeTakesCatalogues::getNamedResource(resource.c_str(), size);
    if (data == nullptr || size <= 0)
    {
        return std::nullopt;
    }
    return Catalogue::parse(juce::String::fromUTF8(data, size));
}
} // namespace

Localisation& current()
{
    static Localisation localisation(embedded("en").value_or(Catalogue{}));
    return localisation;
}

bool setLanguage(std::string_view code)
{
    if (code == pseudolocale::code)
    {
        const auto english = embedded("en");
        const auto pseudo = Catalogue::pseudolocalised(english.value_or(Catalogue{}));
        current().choose(code, &pseudo);
        return true;
    }
    if (languageOf(code) == "en")
    {
        current().choose(code, nullptr);
        return true;
    }
    const auto catalogue = embedded(code);
    if (!catalogue.has_value())
    {
        current().choose("en", nullptr);
        return false;
    }
    current().choose(code, &*catalogue);
    return true;
}
} // namespace l10n
