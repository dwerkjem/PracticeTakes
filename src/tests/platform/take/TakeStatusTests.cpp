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

TEST_CASE("every state of a take has its phrase", "[take][status]")
{
    Take take;
    take.id = 3;
    take.sampleRate = 100.0;
    take.lastFrame = 4200;
    CHECK(describe(take) == "Starting");

    take.lifecycle.apply(TakeEvent::countInStarted);
    CHECK(describe(take) == "Counting in");
    take.lifecycle.apply(TakeEvent::performanceStarted);
    CHECK(describe(take) == "Recording 0:42");
    take.lifecycle.apply(TakeEvent::paused);
    CHECK(describe(take) == "Paused at 0:42");

    Take stopped = take;
    stopped.stoppedInBar = 13;
    stopped.lifecycle.apply(TakeEvent::stopped);
    CHECK(describe(stopped) == "Take 3 stopped in bar 14, 0:42");

    Take complete = take;
    complete.lifecycle.apply(TakeEvent::reachedEnd);
    CHECK(describe(complete) == "Take 3 complete, 0:42");

    Take failed = take;
    failed.lifecycle.apply(TakeEvent::failed, "another score was opened");
    CHECK(describe(failed) == "Take 3 failed: another score was opened");

    Take discarded;
    discarded.id = 4;
    discarded.lifecycle.apply(TakeEvent::cancelled);
    CHECK(describe(discarded) == "Take 4 discarded");
}
