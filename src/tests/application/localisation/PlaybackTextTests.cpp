#include <catch2/catch_test_macros.hpp>

#include "application/localisation/PlaybackText.h"

TEST_CASE("a track is named by its part, and its hand or staff", "[playback][localisation]")
{
    REQUIRE(l10n::setLanguage("en"));
    using transport::TrackLabel;

    CHECK(l10n::trackName(TrackLabel{"Voice", 1, 1, 1}) == "Voice");
    CHECK(l10n::trackName(TrackLabel{"Piano", 2, 1, 2}) == "Piano, right hand");
    CHECK(l10n::trackName(TrackLabel{"Piano", 2, 2, 2}) == "Piano, left hand");
    CHECK(l10n::trackName(TrackLabel{"", 3, 3, 3}) == "Part 3, staff 3");
    CHECK(l10n::trackName(TrackLabel{"", 4, 1, 1}) == "Part 4");
}
