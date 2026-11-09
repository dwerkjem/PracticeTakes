#include <catch2/catch_test_macros.hpp>

#include "application/localisation/LocalisationRules.h"

#include <limits>
#include <string>
#include <vector>

using l10n::formatNamed;
using l10n::formatNumber;
using l10n::languageOf;
using l10n::numberStyleFor;
using l10n::PluralCategory;
using l10n::pluralCategory;

TEST_CASE("a language code is read without its region", "[localisation]")
{
    CHECK(languageOf("pt-BR") == "pt");
    CHECK(languageOf("pt_BR") == "pt");
    CHECK(languageOf("de") == "de");
    CHECK(languageOf("") == "");
}

TEST_CASE("placeholders are filled by name, in the pattern's order", "[localisation]")
{
    const l10n::NamedArguments arguments{{"bar", "12"}, {"part", "Violin"}};
    CHECK(formatNamed("{part}, bar {bar}", arguments) == "Violin, bar 12");
    // A translation that puts them the other way round gets them the other
    // way round, without the call site knowing.
    CHECK(formatNamed("Takt {bar} ({part})", arguments) == "Takt 12 (Violin)");
    // One argument used twice is filled twice.
    CHECK(formatNamed("{bar}/{bar}", arguments) == "12/12");
}

TEST_CASE("a placeholder with no argument stays visible", "[localisation]")
{
    CHECK(formatNamed("bar {bar} of {total}", {{"bar", "3"}}) == "bar 3 of {total}");
    CHECK(formatNamed("an open { brace", {}) == "an open { brace");
    CHECK(formatNamed("no placeholders", {{"bar", "3"}}) == "no placeholders");
}

TEST_CASE("English, German and the rest give one alone to one", "[localisation]")
{
    for (const auto* code : {"en", "de", "es", "it", "pt", "pt-BR"})
    {
        CAPTURE(code);
        CHECK(pluralCategory(code, 0) == PluralCategory::other);
        CHECK(pluralCategory(code, 1) == PluralCategory::one);
        CHECK(pluralCategory(code, 2) == PluralCategory::other);
        CHECK(pluralCategory(code, -1) == PluralCategory::one);
        CHECK(pluralCategory(code, 21) == PluralCategory::other);
    }
}

TEST_CASE("French puts zero with one", "[localisation]")
{
    CHECK(pluralCategory("fr", 0) == PluralCategory::one);
    CHECK(pluralCategory("fr", 1) == PluralCategory::one);
    CHECK(pluralCategory("fr", 2) == PluralCategory::other);
    CHECK(pluralCategory("fr-CA", 0) == PluralCategory::one);
}

TEST_CASE("an unknown language takes the English plural rule", "[localisation]")
{
    CHECK(pluralCategory("xx", 1) == PluralCategory::one);
    CHECK(pluralCategory("xx", 0) == PluralCategory::other);
}

TEST_CASE("a number is written with the language's separators", "[localisation]")
{
    CHECK(formatNumber(440.0, 1, numberStyleFor("en")) == "440.0");
    CHECK(formatNumber(440.0, 1, numberStyleFor("de")) == "440,0");
    CHECK(formatNumber(440.0, 1, numberStyleFor("fr")) == "440,0");
    CHECK(formatNumber(440.0, 1, numberStyleFor("es")) == "440,0");
    CHECK(formatNumber(440.0, 1, numberStyleFor("pt-BR")) == "440,0");

    CHECK(formatNumber(1234567.25, 2, numberStyleFor("en")) == "1,234,567.25");
    CHECK(formatNumber(1234567.25, 2, numberStyleFor("de")) == "1.234.567,25");
    CHECK(
        formatNumber(12345.0, 0, numberStyleFor("fr")) == "12\xE2\x80\xAF"
                                                          "345");
}

TEST_CASE("Spanish groups only from five digits", "[localisation]")
{
    CHECK(formatNumber(1000.0, 0, numberStyleFor("es")) == "1000");
    CHECK(formatNumber(10000.0, 0, numberStyleFor("es")) == "10.000");
    CHECK(formatNumber(1000.0, 0, numberStyleFor("de")) == "1.000");
}

TEST_CASE("a number is rounded half away from zero", "[localisation]")
{
    const auto english = numberStyleFor("en");
    CHECK(formatNumber(2.5, 0, english) == "3");
    CHECK(formatNumber(-2.5, 0, english) == "-3");
    CHECK(formatNumber(0.25, 1, english) == "0.3");
    CHECK(formatNumber(9.96, 1, english) == "10.0");
    CHECK(formatNumber(0.04, 1, english) == "0.0");
    CHECK(formatNumber(-0.04, 1, english) == "0.0");
    CHECK(formatNumber(-12.5, 1, english) == "-12.5");
    CHECK(formatNumber(0.05, 2, english) == "0.05");
}

TEST_CASE("a number that is not one is still written", "[localisation]")
{
    const auto english = numberStyleFor("en");
    CHECK(formatNumber(std::numeric_limits<double>::quiet_NaN(), 1, english) == "NaN");
    CHECK(formatNumber(std::numeric_limits<double>::infinity(), 1, english) == "\xE2\x88\x9E");
    CHECK(formatNumber(-std::numeric_limits<double>::infinity(), 1, english) == "-\xE2\x88\x9E");
}

TEST_CASE("the language chosen, the system's, or English", "[localisation]")
{
    const std::vector<std::string> available{"en", "de"};
    CHECK(l10n::chooseLanguage("de", "fr", available) == "de");
    CHECK(l10n::chooseLanguage("de-AT", "fr", available) == "de-AT");
    // Automatic: the system's, when there is a catalogue for it.
    CHECK(l10n::chooseLanguage("", "de", available) == "de");
    CHECK(l10n::chooseLanguage("", "fr", available) == "en");
    // A choice whose catalogue is gone falls back the same way.
    CHECK(l10n::chooseLanguage("it", "de", available) == "de");
    CHECK(l10n::chooseLanguage("", "", {}) == "en");
}
