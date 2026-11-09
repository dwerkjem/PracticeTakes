#include <catch2/catch_test_macros.hpp>

#include "application/localisation/Localisation.h"

#include <PracticeTakesCatalogues.h>

using l10n::Catalogue;
using l10n::Localisation;

namespace
{
Catalogue english()
{
    return Catalogue::parse(R"(# A comment line, ignored.
language: English

"tuner.title" = "Tuner"
"score.where" = "Bar {bar}, {part}"
"score.bars.one" = "{count} bar"
"score.bars.other" = "{count} bars"
"only.english" = "Only in English"
"quoted" = "Say \"A\"\nthen B"
)");
}

Catalogue german()
{
    return Catalogue::parse(juce::String::fromUTF8(R"(language: Deutsch
"tuner.title" = "Stimmgerät"
"score.where" = "{part}, Takt {bar}"
"score.bars.one" = "{count} Takt"
"score.bars.other" = "{count} Takte"
)"));
}

Catalogue french()
{
    return Catalogue::parse(R"(language: Francais
"score.bars.one" = "{count} mesure"
"score.bars.other" = "{count} mesures"
)");
}
} // namespace

TEST_CASE("a catalogue is read from JUCE's format", "[localisation]")
{
    const auto catalogue = english();
    CHECK(catalogue.languageName() == "English");
    CHECK(catalogue.entries().size() == 6);
    REQUIRE(catalogue.find("tuner.title") != nullptr);
    CHECK(*catalogue.find("tuner.title") == "Tuner");
    CHECK(*catalogue.find("quoted") == "Say \"A\"\nthen B");
    CHECK(catalogue.find("missing") == nullptr);
}

TEST_CASE("English is used when nothing else is chosen", "[localisation]")
{
    const Localisation localisation(english());
    CHECK(localisation.languageCode() == "en");
    CHECK(localisation.text("tuner.title") == "Tuner");
}

TEST_CASE("the chosen language falls back to English, then to the identifier", "[localisation]")
{
    Localisation localisation(english());
    const auto catalogue = german();
    localisation.choose("de", &catalogue);

    CHECK(localisation.text("tuner.title") == juce::String::fromUTF8("Stimmgerät"));
    // Missing from German: English.
    CHECK(localisation.text("only.english") == "Only in English");
    // Missing from both: the identifier, visibly wrong rather than blank.
    CHECK(localisation.text("no.such.key") == "no.such.key");
}

TEST_CASE("a translation puts the placeholders in its own order", "[localisation]")
{
    Localisation localisation(english());
    CHECK(
        localisation.format("score.where", {{"bar", "12"}, {"part", "Violin"}}) ==
        "Bar 12, Violin");

    const auto catalogue = german();
    localisation.choose("de", &catalogue);
    CHECK(
        localisation.format("score.where", {{"bar", "12"}, {"part", "Violine"}}) ==
        "Violine, Takt 12");
}

TEST_CASE("a quantity takes the form its language's plural rule picks", "[localisation]")
{
    Localisation localisation(english());
    CHECK(localisation.plural("score.bars", 0) == "0 bars");
    CHECK(localisation.plural("score.bars", 1) == "1 bar");
    CHECK(localisation.plural("score.bars", 2) == "2 bars");
    CHECK(localisation.plural("score.bars", 1200) == "1,200 bars");

    const auto deutsch = german();
    localisation.choose("de", &deutsch);
    CHECK(localisation.plural("score.bars", 0) == "0 Takte");
    CHECK(localisation.plural("score.bars", 1) == "1 Takt");
    CHECK(localisation.plural("score.bars", 1200) == "1.200 Takte");

    const auto francais = french();
    localisation.choose("fr", &francais);
    CHECK(localisation.plural("score.bars", 0) == "0 mesure");
    CHECK(localisation.plural("score.bars", 1) == "1 mesure");
    CHECK(localisation.plural("score.bars", 2) == "2 mesures");
}

TEST_CASE("a plural form a catalogue lacks falls back to its other form", "[localisation]")
{
    Localisation localisation(english());
    const auto catalogue = Catalogue::parse(R"(language: Test
"score.bars.other" = "{count} x"
)");
    localisation.choose("de", &catalogue);
    CHECK(localisation.plural("score.bars", 1) == "1 x");

    // A key with no forms anywhere shows its identifier.
    CHECK(localisation.plural("no.such", 3) == "no.such.other");
}

TEST_CASE("the language decides numbers even where its catalogue falls back", "[localisation]")
{
    Localisation localisation(english());
    const auto catalogue = Catalogue::parse("language: Deutsch\n");
    localisation.choose("de-AT", &catalogue);
    CHECK(localisation.languageCode() == "de-AT");
    CHECK(localisation.number(440.0, 1) == "440,0");
    // English text, German count.
    CHECK(localisation.plural("score.bars", 1200) == "1.200 bars");
}

TEST_CASE("choosing English, or no catalogue, means English", "[localisation]")
{
    Localisation localisation(english());
    const auto catalogue = german();
    localisation.choose("de", &catalogue);
    localisation.choose("en-GB", &catalogue);
    CHECK(localisation.text("tuner.title") == "Tuner");

    localisation.choose("", nullptr);
    CHECK(localisation.languageCode() == "en");
}

TEST_CASE("an unknown language runs in English rather than failing", "[localisation]")
{
    CHECK_FALSE(l10n::setLanguage("xx"));
    CHECK(l10n::current().languageCode() == "en");
    CHECK(l10n::number(440.0, 1) == "440.0");
    CHECK(l10n::text("no.such.key") == "no.such.key");

    CHECK(l10n::setLanguage("en"));
    CHECK(l10n::current().languageCode() == "en");
}

TEST_CASE("the English catalogue is embedded", "[localisation]")
{
    int size = 0;
    const char* data = PracticeTakesCatalogues::getNamedResource("en_txt", size);
    REQUIRE(data != nullptr);
    CHECK(Catalogue::parse(juce::String::fromUTF8(data, size)).languageName() == "English");
}

TEST_CASE("the embedded languages are listed by their own names, English first", "[localisation]")
{
    const auto languages = l10n::availableLanguages();
    REQUIRE_FALSE(languages.empty());
    CHECK(languages.front().code == "en");
    CHECK(languages.front().name == "English");
}
