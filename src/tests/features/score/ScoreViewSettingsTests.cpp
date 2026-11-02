#include <catch2/catch_test_macros.hpp>

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

    CHECK(decode({1, ""}) == Settings{});
    CHECK(decode({1, ";;=;view"}) == Settings{});
    CHECK(decode({1, "size=-4"}) == Settings{});
}

TEST_CASE("another version's payload is not read", "[score][view-settings]")
{
    CHECK(decode({2, "view=horizontal;size=13"}) == Settings{});
}
