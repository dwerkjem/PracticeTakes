#include "features/performance/RealTimeEventCapture.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

TEST_CASE("real-time capture preserves callback events and deadline status")
{
    performance::RealTimeEventCapture<4> capture;

    REQUIRE(capture.record(100, 20, 25, 1, 2));
    REQUIRE(capture.record(200, 30, 25));

    std::array<performance::AudioCallbackEvent, 4> events;
    REQUIRE(capture.drain(events) == 2);
    REQUIRE(events[0].sequence == 0);
    REQUIRE(events[0].startedAtNanoseconds == 100);
    REQUIRE(events[0].durationNanoseconds == 20);
    REQUIRE(events[0].dropoutCount == 1);
    REQUIRE(events[0].underrunCount == 2);
    REQUIRE_FALSE(events[0].deadlineMissed);
    REQUIRE(events[1].sequence == 1);
    REQUIRE(events[1].deadlineMissed);
}

TEST_CASE("real-time capture wraps after draining without reordering events")
{
    performance::RealTimeEventCapture<3> capture;
    std::array<performance::AudioCallbackEvent, 2> firstDrain;
    std::array<performance::AudioCallbackEvent, 3> secondDrain;

    REQUIRE(capture.record(10, 1, 2));
    REQUIRE(capture.record(20, 1, 2));
    REQUIRE(capture.record(30, 1, 2));
    REQUIRE(capture.drain(firstDrain) == 2);
    REQUIRE(capture.record(40, 1, 2));
    REQUIRE(capture.record(50, 1, 2));

    REQUIRE(capture.drain(secondDrain) == 3);
    REQUIRE(secondDrain[0].startedAtNanoseconds == 30);
    REQUIRE(secondDrain[1].startedAtNanoseconds == 40);
    REQUIRE(secondDrain[2].startedAtNanoseconds == 50);
}

TEST_CASE("real-time capture drops newest events when its fixed storage is full")
{
    performance::RealTimeEventCapture<2> capture;
    std::array<performance::AudioCallbackEvent, 2> events;

    REQUIRE(capture.record(10, 1, 2));
    REQUIRE(capture.record(20, 1, 2));
    REQUIRE_FALSE(capture.record(30, 1, 2));
    REQUIRE(capture.available() == 2);
    REQUIRE(capture.droppedEvents() == 1);
    REQUIRE(capture.drain(events) == 2);
    REQUIRE(events[0].startedAtNanoseconds == 10);
    REQUIRE(events[1].startedAtNanoseconds == 20);
}