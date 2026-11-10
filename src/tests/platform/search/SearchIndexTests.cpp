#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "platform/search/SearchIndex.h"

// The search index through its C++ face: what the reader types and what comes
// back. Tantivy is real here, not a double -- the crate is linked into the
// test target as it is into the application.
namespace
{
std::vector<std::string> ids(const std::vector<search::SearchHit>& hits)
{
    std::vector<std::string> result;
    result.reserve(hits.size());
    for (const auto& hit : hits)
    {
        result.push_back(hit.id);
    }
    return result;
}

search::SearchIndex filled()
{
    search::SearchIndex index;
    REQUIRE(index.usable());
    REQUIRE(index.replace(
        "setting",
        {{"settings.practice.metronome", "Metronome", "Practice", "click beat",
          "A click on every beat"},
         {"settings.practice.count-in", "Count-in", "Practice", "", "Bars of clicks before a take"},
         {"settings.appearance.theme", "Theme", "Appearance", "dark light", ""}}));
    REQUIRE(index.replace(
        "song", {{"/library/Songs/mahler.mxl", "Die zwei blauen Augen", "Gustav Mahler", "",
                  "Voice, Piano"}}));
    return index;
}
} // namespace

TEST_CASE("a search finds by title, half-typed, and a letter out", "[search]")
{
    const auto index = filled();
    CHECK(
        ids(index.search("metro", 10)) == std::vector<std::string>{"settings.practice.metronome"});
    CHECK(
        ids(index.search("metronme", 10)) ==
        std::vector<std::string>{"settings.practice.metronome"});
    // Accents and case do not matter.
    CHECK(ids(index.search("BLAUEN", 10)) == std::vector<std::string>{"/library/Songs/mahler.mxl"});
}

TEST_CASE("a hit carries what to show and what to act on", "[search]")
{
    const auto hits = filled().search("mahler", 10);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].kind == "song");
    CHECK(hits[0].title == "Die zwei blauen Augen");
    CHECK(hits[0].detail == "Gustav Mahler");
    CHECK(hits[0].score > 0.0f);
}

TEST_CASE("every word narrows, and the title outranks the body", "[search]")
{
    const auto index = filled();
    // "click" is in the metronome's keywords and body, and in the count-in's
    // body; the metronome is first.
    const auto clicks = ids(index.search("click", 10));
    REQUIRE(clicks.size() == 2);
    CHECK(clicks[0] == "settings.practice.metronome");
    CHECK(
        ids(index.search("click take", 10)) ==
        std::vector<std::string>{"settings.practice.count-in"});
    CHECK(index.search("theme mahler", 10).empty());
}

TEST_CASE("replacing a kind leaves the other kinds alone", "[search]")
{
    auto index = filled();
    REQUIRE(index.replace("song", {{"/library/Songs/aria.musicxml", "Aria", "Bach", "", ""}}));
    CHECK(index.search("mahler", 10).empty());
    CHECK(
        ids(index.search("aria", 10)) == std::vector<std::string>{"/library/Songs/aria.musicxml"});
    CHECK(ids(index.search("theme", 10)) == std::vector<std::string>{"settings.appearance.theme"});
    REQUIRE(index.replace("song", {}));
    CHECK(index.search("aria", 10).empty());
}

TEST_CASE("what a reader types cannot break a search", "[search]")
{
    const auto index = filled();
    CHECK(index.search("", 10).empty());
    CHECK(index.search("   ", 10).empty());
    CHECK(index.search("C# \"unclosed (paren AND OR -", 10).empty());
    CHECK(index.search(std::string("theme\0hidden", 12), 10).size() == 1);
    CHECK(index.search("theme", 0).empty());
    // At most the limit.
    CHECK(index.search("practice", 1).size() == 1);
}
