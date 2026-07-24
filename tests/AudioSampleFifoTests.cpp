#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/AudioSampleFifo.h"

#include <array>

TEST_CASE("audio FIFO preserves order across its wrap boundary", "[audio][fifo]")
{
    AudioSampleFifo<5> fifo;
    const std::array first{1.0f, 2.0f, 3.0f};
    const std::array second{4.0f, 5.0f, 6.0f, 7.0f};
    std::array<float, 5> output{};

    REQUIRE(fifo.push(first.data(), first.size()));
    CHECK(fifo.pop(output.data(), 2) == 2);
    REQUIRE(fifo.push(second.data(), second.size()));
    CHECK(fifo.pop(output.data(), output.size()) == output.size());

    CHECK(output == std::array{3.0f, 4.0f, 5.0f, 6.0f, 7.0f});
}

TEST_CASE("audio FIFO drops one complete newest block when full", "[audio][fifo]")
{
    AudioSampleFifo<4> fifo;
    const std::array queued{1.0f, 2.0f, 3.0f};
    const std::array overflow{8.0f, 9.0f};
    std::array<float, 4> output{};

    REQUIRE(fifo.push(queued.data(), queued.size()));
    CHECK_FALSE(fifo.push(overflow.data(), overflow.size()));
    CHECK(fifo.droppedBlocks() == 1);
    CHECK(fifo.droppedSamples() == overflow.size());
    CHECK(fifo.pop(output.data(), output.size()) == queued.size());
    CHECK(output[0] == Catch::Approx(1.0f));
    CHECK(output[1] == Catch::Approx(2.0f));
    CHECK(output[2] == Catch::Approx(3.0f));
}

TEST_CASE("audio FIFO applies input gain before analysis", "[audio][fifo]")
{
    AudioSampleFifo<4> fifo;
    const std::array input{0.25f, -0.5f};
    std::array<float, 2> output{};

    REQUIRE(fifo.push(input.data(), input.size(), 2.0f));
    REQUIRE(fifo.pop(output.data(), output.size()) == output.size());
    CHECK(output[0] == Catch::Approx(0.5f));
    CHECK(output[1] == Catch::Approx(-1.0f));
}

TEST_CASE("a full consumer does not affect another consumer", "[audio][fifo]")
{
    AudioSampleFifo<2> slowConsumer;
    AudioSampleFifo<2> currentConsumer;
    const std::array first{1.0f, 2.0f};
    const std::array next{3.0f, 4.0f};
    std::array<float, 2> output{};

    REQUIRE(slowConsumer.push(first.data(), first.size()));
    CHECK_FALSE(slowConsumer.push(next.data(), next.size()));

    REQUIRE(currentConsumer.push(next.data(), next.size()));
    REQUIRE(currentConsumer.pop(output.data(), output.size()) == output.size());
    CHECK(output == next);
}
