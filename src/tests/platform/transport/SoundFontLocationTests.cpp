#include <catch2/catch_test_macros.hpp>

#include "platform/transport/SoundFontLocation.h"

TEST_CASE("the SoundFont is looked for where each platform installs it", "[transport][playback]")
{
    const auto beside = transport::soundFontCandidates("/opt/build/bin");
    REQUIRE(beside.size() == 3);
    CHECK(beside[0] == "/opt/build/bin/sounds/GeneralUser-GS.sf2");
    CHECK(beside[1] == "/opt/build/bin/../share/practice-takes/sounds/GeneralUser-GS.sf2");
    CHECK(beside[2] == "/opt/build/bin/../Resources/sounds/GeneralUser-GS.sf2");
}
