#include <catch2/catch_test_macros.hpp>

#include "application/shell/WorkspaceStartupSequence.h"

#include <string>
#include <vector>

TEST_CASE(
    "workspace restoration completes before window visibility and deferred audio",
    "[workspace][startup][audio]")
{
    std::vector<std::string> events;

    WorkspaceStartupSequence::run(
        [&events] { events.emplace_back("workspace"); },
        [&events] { events.emplace_back("visible"); }, [&events] { events.emplace_back("audio"); });

    CHECK(events == std::vector<std::string>{"workspace", "visible", "audio"});
}
