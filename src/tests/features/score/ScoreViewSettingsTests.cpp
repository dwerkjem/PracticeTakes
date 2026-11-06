#include <catch2/catch_test_macros.hpp>

#include "application/tools/BuiltInToolCatalog.h"
#include "features/score/ScoreViewSettings.h"

using namespace score_view;

TEST_CASE("a new Score View opens in Page view at the old medium size", "[score][view-settings]")
{
    const Settings defaults;

    CHECK(defaults.view == ViewMode::page);
    CHECK(sizeLadder.at(defaults.sizeStep) == 7.0);
}

TEST_CASE("the old three sizes are all on the ladder", "[score][view-settings]")
{
    // Small, Medium and Large were 5.2, 7.0 and 9.4 pixels per space.
    for (const double old : {5.2, 7.0, 9.4})
    {
        INFO("size " << old);
        CHECK(sizeLadder.at(nearestSizeStep(old)) == old);
    }
}

TEST_CASE("the ladder runs smallest to largest", "[score][view-settings]")
{
    for (std::size_t step = 1; step < sizeLadder.size(); ++step)
    {
        CHECK(sizeLadder.at(step) > sizeLadder.at(step - 1));
    }
}

TEST_CASE("settings survive a round trip", "[score][view-settings]")
{
    for (const ViewMode view : {ViewMode::page, ViewMode::vertical, ViewMode::horizontal})
    {
        for (std::size_t step = 0; step < sizeLadder.size(); ++step)
        {
            const Settings saved{view, step};

            CHECK(decode(encode(saved)) == saved);
        }
    }
}

TEST_CASE("the payload is readable", "[score][view-settings]")
{
    const ToolSettingsPayload payload = encode(Settings{ViewMode::horizontal, 6});

    CHECK(payload.version == settingsVersion);
    CHECK(payload.data == "view=horizontal;size=9.4");
}

TEST_CASE("a size no longer offered snaps to the nearest one", "[score][view-settings]")
{
    CHECK(sizeLadder.at(decode({1, "view=vertical;size=7.3"}).sizeStep) == 7.0);
    CHECK(sizeLadder.at(decode({1, "view=vertical;size=100"}).sizeStep) == 13.0);
    CHECK(sizeLadder.at(decode({1, "view=vertical;size=0.5"}).sizeStep) == 4.0);
}

TEST_CASE("what cannot be read falls back field by field", "[score][view-settings]")
{
    // A good view beside a bad size keeps the view.
    const Settings badSize = decode({1, "view=horizontal;size=huge"});
    CHECK(badSize.view == ViewMode::horizontal);
    CHECK(badSize.sizeStep == defaultSizeStep);

    // And the other way round.
    const Settings badView = decode({1, "view=sideways;size=11"});
    CHECK(badView.view == ViewMode::page);
    CHECK(sizeLadder.at(badView.sizeStep) == 11.0);

    // Only plain digits with an optional point are a size.
    CHECK(decode({1, "size=1e1"}).sizeStep == defaultSizeStep);
    CHECK(decode({1, "size=+9.4"}).sizeStep == defaultSizeStep);
    CHECK(decode({1, "size=9.4.1"}).sizeStep == defaultSizeStep);
    CHECK(decode({1, "size=."}).sizeStep == defaultSizeStep);
    CHECK(sizeLadder.at(decode({1, "size=11."}).sizeStep) == 11.0);

    CHECK(decode({1, ""}) == Settings{});
    CHECK(decode({1, ";;=;view"}) == Settings{});
    CHECK(decode({1, "size=-4"}) == Settings{});
}

TEST_CASE("another version's payload is not read", "[score][view-settings]")
{
    CHECK(decode({2, "view=horizontal;size=13"}) == Settings{});
}

TEST_CASE("the catalog restores what the Score View saves", "[score][view-settings]")
{
    // The shell discards a saved payload whose version is not the tool's
    // settingsVersion, so a mismatch would silently forget every view.
    const ToolDefinition* definition = builtInToolCatalog().find("score-view");

    REQUIRE(definition != nullptr);
    CHECK(definition->settingsVersion == std::optional<int>(settingsVersion));
}

// Hidden parts (score-part-selection, design.md decision 6).
TEST_CASE("hidden parts survive a round trip", "[score][view-settings][part-selection]")
{
    Settings saved{ViewMode::page, defaultSizeStep};
    saved.hiddenParts = {
        {"P1", "Voice"},
        // Every character the payload uses as a separator, and the escape.
        {"P2", "Alto: 1st, solo; =100% loud"},
        {"P3", ""}};

    CHECK(decode(encode(saved)) == saved);
}

TEST_CASE(
    "hidden parts are written by id and escaped name",
    "[score][view-settings][part-selection]")
{
    Settings saved{ViewMode::horizontal, 6};
    saved.hiddenParts = {{"P1", "Voice"}, {"P3", "Alto, 2"}};

    CHECK(encode(saved).data == "view=horizontal;size=9.4;hidden=P1:Voice,P3:Alto%2C 2");
}

TEST_CASE("nothing hidden writes nothing extra", "[score][view-settings][part-selection]")
{
    // So a payload saved before parts could be hidden is exactly what a Score
    // View with nothing hidden writes now.
    CHECK(encode(Settings{ViewMode::page, defaultSizeStep}).data == "view=page;size=7");
    CHECK(decode({settingsVersion, "view=page;size=7"}).hiddenParts.empty());
}

TEST_CASE(
    "an unreadable hidden entry is skipped and the rest kept",
    "[score][view-settings][part-selection]")
{
    const Settings settings = decode(
        {settingsVersion,
         "view=vertical;hidden=P1:Voice,:NoId,NoColon,P4:Bad%4,P5:Bad%ZZ,P6:Tenor"});

    CHECK(settings.view == ViewMode::vertical);
    CHECK(
        settings.hiddenParts == std::vector<engraving::PartKey>{{"P1", "Voice"}, {"P6", "Tenor"}});
}
