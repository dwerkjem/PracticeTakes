#include <catch2/catch_test_macros.hpp>

#include "platform/take/TakeStatus.h"

using namespace take;

TEST_CASE("a duration is minutes and seconds", "[take][status]")
{
    CHECK(formatDuration(0.0) == "0:00");
    CHECK(formatDuration(12.9) == "0:12");
    CHECK(formatDuration(65.0) == "1:05");
    CHECK(formatDuration(-3.0) == "0:00");
}
