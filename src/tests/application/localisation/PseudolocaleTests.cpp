#include <catch2/catch_test_macros.hpp>

#include "application/localisation/Localisation.h"
#include "application/localisation/Pseudolocale.h"
#include "application/shell/ui/ShellWindowTitles.h"
#include "application/tools/BuiltInToolCatalog.h"

#include <string>

namespace
{
[[nodiscard]] std::size_t codePoints(const std::string& text)
{
    std::size_t count = 0;
    for (const char character : text)
    {
        count += (static_cast<unsigned char>(character) & 0xC0u) != 0x80u ? 1 : 0;
    }
    return count;
}
} // namespace

TEST_CASE("the pseudolocale brackets, accents and lengthens", "[localisation][pseudolocale]")
{
    const std::string settings = pseudolocale::transform("Settings");
    CHECK(settings == "[S\xC3\xA9tt\xC3\xAE\xC3\xB1gs ~~~~]");

    // About 40% longer than the English, counting what a reader sees.
    const std::string english = "Choose the application theme used by every open window.";
    const std::string expanded = pseudolocale::transform(english);
    CHECK(codePoints(expanded) >= codePoints(english) * 7 / 5);
    CHECK(expanded.front() == '[');
    CHECK(expanded.back() == ']');

    // Even one letter grows.
    CHECK(pseudolocale::transform("A") == "[\xC3\x81 ~]");
}

TEST_CASE("placeholders and line breaks survive the pseudolocale", "[localisation][pseudolocale]")
{
    const std::string pattern = pseudolocale::transform("Bar {bar}, beat {beat}\nnext");
    CHECK(pattern.find("{bar}") != std::string::npos);
    CHECK(pattern.find("{beat}") != std::string::npos);
    CHECK(pattern.find('\n') != std::string::npos);
    CHECK(l10n::formatNamed(pattern, {{"bar", "3"}, {"beat", "2"}}).find("3") != std::string::npos);
}

TEST_CASE("the pseudolocale is chosen like a language", "[localisation][pseudolocale]")
{
    REQUIRE(l10n::setLanguage(pseudolocale::code));
    CHECK(l10n::text("settings.reset.confirm.cancel").startsWith("["));
    CHECK(l10n::plural("score.view.diagnostics", 2).contains("2"));
    // A key nothing defines is not bracketed: that is what a missed string looks like.
    CHECK(l10n::text("no.such.key") == "no.such.key");
    // Numbers stay English: the pseudolocale is about length, not separators.
    CHECK(l10n::number(1234.5, 1) == "1,234.5");

    REQUIRE(l10n::setLanguage("en"));
    CHECK(l10n::text("settings.reset.confirm.cancel") == "Cancel");
}

TEST_CASE(
    "a window's English title is its catalogue entry, so the harness can find it in any language",
    "[localisation][pseudolocale]")
{
    REQUIRE(l10n::setLanguage("en"));
    CHECK(l10n::text(shellwindows::settingsKey).toStdString() == shellwindows::settings);
    CHECK(l10n::text(shellwindows::feedbackKey).toStdString() == shellwindows::feedback);
    CHECK(l10n::text(shellwindows::scoreImportKey).toStdString() == shellwindows::scoreImport);

    for (const auto& tool : builtInToolCatalog().tools())
    {
        CAPTURE(tool.id);
        CHECK(l10n::text(toolNameKey(tool.id)).toStdString() == tool.displayName);
    }
}
