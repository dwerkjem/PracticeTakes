#include <catch2/catch_test_macros.hpp>

#include "platform/take/TakeLifecycle.h"

using namespace take;

TEST_CASE("a take counts in, performs and completes", "[take][lifecycle]")
{
    TakeLifecycle lifecycle;
    CHECK(lifecycle.state() == TakeState::ready);
    CHECK(isLive(lifecycle.state()));

    CHECK(lifecycle.apply(TakeEvent::countInStarted));
    CHECK(lifecycle.state() == TakeState::countingIn);
    CHECK_FALSE(lifecycle.hasPerformed());

    CHECK(lifecycle.apply(TakeEvent::performanceStarted));
    CHECK(lifecycle.state() == TakeState::performing);
    CHECK(lifecycle.hasPerformed());

    CHECK(lifecycle.apply(TakeEvent::reachedEnd));
    CHECK(lifecycle.state() == TakeState::completed);
    CHECK(isKept(lifecycle.state()));
}

TEST_CASE("a take stopped part-way is kept, but not as completed", "[take][lifecycle]")
{
    TakeLifecycle lifecycle;
    lifecycle.apply(TakeEvent::performanceStarted);
    CHECK(lifecycle.apply(TakeEvent::stopped));
    CHECK(lifecycle.state() == TakeState::stoppedEarly);
    CHECK(isKept(lifecycle.state()));
}

TEST_CASE("a take stopped before it was played is cancelled", "[take][lifecycle]")
{
    TakeLifecycle ready;
    CHECK(ready.apply(TakeEvent::stopped));
    CHECK(ready.state() == TakeState::cancelled);
    CHECK_FALSE(isKept(ready.state()));

    TakeLifecycle countingIn;
    countingIn.apply(TakeEvent::countInStarted);
    countingIn.apply(TakeEvent::paused);
    CHECK(countingIn.state() == TakeState::paused);
    CHECK(countingIn.apply(TakeEvent::stopped));
    CHECK(countingIn.state() == TakeState::cancelled);
}

TEST_CASE("a paused take resumes by counting in again or performing", "[take][lifecycle]")
{
    TakeLifecycle lifecycle;
    lifecycle.apply(TakeEvent::performanceStarted);
    CHECK(lifecycle.apply(TakeEvent::paused));
    CHECK(lifecycle.state() == TakeState::paused);
    CHECK(lifecycle.apply(TakeEvent::countInStarted));
    CHECK(lifecycle.state() == TakeState::countingIn);
    CHECK(lifecycle.apply(TakeEvent::performanceStarted));
    CHECK(lifecycle.apply(TakeEvent::paused));
    CHECK(lifecycle.apply(TakeEvent::reachedEnd));
    CHECK(lifecycle.state() == TakeState::completed);
}

TEST_CASE("events that do not apply change nothing", "[take][lifecycle]")
{
    TakeLifecycle lifecycle;
    CHECK_FALSE(lifecycle.apply(TakeEvent::paused));
    CHECK_FALSE(lifecycle.apply(TakeEvent::reachedEnd));
    CHECK(lifecycle.state() == TakeState::ready);

    lifecycle.apply(TakeEvent::performanceStarted);
    CHECK_FALSE(lifecycle.apply(TakeEvent::countInStarted));
    CHECK_FALSE(lifecycle.apply(TakeEvent::performanceStarted));
    CHECK(lifecycle.state() == TakeState::performing);
}

TEST_CASE("an ended take stays ended", "[take][lifecycle]")
{
    for (const TakeEvent end : {TakeEvent::stopped, TakeEvent::cancelled, TakeEvent::failed})
    {
        TakeLifecycle lifecycle;
        lifecycle.apply(TakeEvent::performanceStarted);
        lifecycle.apply(end, "a reason");
        const TakeState ended = lifecycle.state();
        CHECK(isEnded(ended));

        for (const TakeEvent later :
             {TakeEvent::countInStarted, TakeEvent::performanceStarted, TakeEvent::paused,
              TakeEvent::reachedEnd, TakeEvent::stopped, TakeEvent::cancelled, TakeEvent::failed})
        {
            CHECK_FALSE(lifecycle.apply(later));
            CHECK(lifecycle.state() == ended);
        }
    }
}

TEST_CASE("a failed take keeps its reason", "[take][lifecycle]")
{
    TakeLifecycle lifecycle;
    lifecycle.apply(TakeEvent::performanceStarted);
    CHECK(lifecycle.apply(TakeEvent::failed, "another score was opened"));
    CHECK(lifecycle.state() == TakeState::failed);
    CHECK(lifecycle.failureReason() == "another score was opened");
    CHECK(isKept(lifecycle.state()));
}
