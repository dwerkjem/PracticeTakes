#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "application/search/SearchCatalogue.h"

// What the search bar can find, from the application's own catalogue and
// tools -- and found through the real index, as the shell finds it.
namespace
{
const search::SearchDocument*
withId(const std::vector<search::SearchDocument>& documents, const std::string& id)
{
    const auto found = std::find_if(
        documents.begin(), documents.end(),
        [&id](const search::SearchDocument& document) { return document.id == id; });
    return found != documents.end() ? &*found : nullptr;
}
} // namespace

TEST_CASE(
    "every control in Settings is searchable, by its words and its panel",
    "[search][catalogue]")
{
    const auto settings = search::settingsDocuments();

    const auto* pitch = withId(settings, "settings.practice.pitch-zones");
    REQUIRE(pitch != nullptr);
    CHECK(pitch->title == "Pitch");
    CHECK(pitch->detail == "Practice");
    CHECK(pitch->body.find("in tune") != std::string::npos);

    // A control's choices are words it is found by.
    const auto* accidentals = withId(settings, "settings.practice.accidentals");
    REQUIRE(accidentals != nullptr);
    CHECK(accidentals->keywords.find("Flats") != std::string::npos);

    // A panel is found by its name and description.
    const auto* practice = withId(settings, "settings.practice.title");
    REQUIRE(practice != nullptr);
    CHECK(practice->detail == "Settings");

    // Nothing that is not a control: a tooltip, or a message with a value.
    for (const auto& document : settings)
    {
        CHECK(document.id.find(".tooltip") == std::string::npos);
        CHECK(document.title.find('{') == std::string::npos);
        CHECK(search::panelOf(document.id) != nullptr);
    }
    CHECK(settings.size() > 30);
}

TEST_CASE("a setting's panel is the one SettingsWindow opens", "[search][catalogue]")
{
    REQUIRE(search::panelOf("settings.audio.input") != nullptr);
    CHECK(search::panelOf("settings.audio.input")->identifier == "audio-device");
    CHECK(search::panelOf("settings.practice.key")->identifier == "practice");
    CHECK(search::panelOf("settings.transfer.title") == nullptr);
    CHECK(search::panelOf("tool.tuner") == nullptr);
}

TEST_CASE("searching the catalogue finds what a reader would look for", "[search][catalogue]")
{
    search::SearchIndex index;
    REQUIRE(index.replace(search::kind::setting, search::settingsDocuments()));
    REQUIRE(index.replace(search::kind::tool, search::toolDocuments()));

    const auto first = [&index](const std::string& query)
    {
        const auto hits = index.search(query, 5);
        return hits.empty() ? std::string() : hits.front().kind + ":" + hits.front().id;
    };
    CHECK(first("tuner") == "tool:tuner");
    // By another name the tool answers to.
    CHECK(first("notation") == "tool:score-view");
    CHECK(first("reference pitch") == "setting:settings.practice.reference");
    CHECK(first("count") == "setting:settings.practice.count-in");
    // Half-typed and misspelt.
    CHECK(first("accid") == "setting:settings.practice.accidentals");
    CHECK(first("refrence") == "setting:settings.practice.reference");
}

TEST_CASE("each kind is introduced by name", "[search][catalogue]")
{
    CHECK(search::kindLabel(search::kind::setting) == "Setting");
    CHECK(search::kindLabel(search::kind::lyric) == "Lyric");
    CHECK(search::kindLabel("nonsense").isEmpty());
}
