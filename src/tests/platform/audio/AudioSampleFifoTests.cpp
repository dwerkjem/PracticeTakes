#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "platform/audio/AudioSampleFifo.h"

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

TEST_CASE("audio FIFO accepts an exact-capacity block", "[audio][fifo]")
{
    AudioSampleFifo<4> fifo;
    const std::array input{1.0f, 2.0f, 3.0f, 4.0f};
    std::array<float, 4> output{};

    REQUIRE(fifo.push(input.data(), input.size()));
    CHECK(fifo.available() == input.size());
    REQUIRE(fifo.pop(output.data(), output.size()) == output.size());
    CHECK(output == input);
    CHECK(fifo.available() == 0);
}

TEST_CASE("audio FIFO supports partial reads", "[audio][fifo]")
{
    AudioSampleFifo<5> fifo;
    const std::array input{1.0f, 2.0f, 3.0f, 4.0f};
    std::array<float, 3> output{};

    REQUIRE(fifo.push(input.data(), input.size()));
    REQUIRE(fifo.pop(output.data(), 2) == 2);
    CHECK(output[0] == Catch::Approx(1.0f));
    CHECK(output[1] == Catch::Approx(2.0f));
    CHECK(fifo.available() == 2);

    REQUIRE(fifo.pop(output.data(), output.size()) == 2);
    CHECK(output[0] == Catch::Approx(3.0f));
    CHECK(output[1] == Catch::Approx(4.0f));
}

TEST_CASE("discarding pending audio preserves drop diagnostics", "[audio][fifo]")
{
    AudioSampleFifo<2> fifo;
    const std::array queued{1.0f, 2.0f};
    const std::array overflow{3.0f};

    REQUIRE(fifo.push(queued.data(), queued.size()));
    REQUIRE_FALSE(fifo.push(overflow.data(), overflow.size()));
    fifo.discardPending();

    CHECK(fifo.available() == 0);
    CHECK(fifo.droppedBlocks() == 1);
    CHECK(fifo.droppedSamples() == 1);
}

TEST_CASE("reset clears buffered audio and drop diagnostics", "[audio][fifo]")
{
    AudioSampleFifo<2> fifo;
    const std::array queued{1.0f, 2.0f};
    const std::array overflow{3.0f};

    REQUIRE(fifo.push(queued.data(), queued.size()));
    REQUIRE_FALSE(fifo.push(overflow.data(), overflow.size()));
    fifo.reset();

    CHECK(fifo.available() == 0);
    CHECK(fifo.droppedBlocks() == 0);
    CHECK(fifo.droppedSamples() == 0);
}

TEST_CASE("null and empty FIFO operations are harmless", "[audio][fifo]")
{
    AudioSampleFifo<2> fifo;
    const std::array input{1.0f};
    std::array<float, 1> output{};

    CHECK(fifo.push(nullptr, 1));
    CHECK(fifo.push(input.data(), 0));
    CHECK(fifo.pop(nullptr, 1) == 0);
    CHECK(fifo.pop(output.data(), 0) == 0);
    CHECK(fifo.available() == 0);
    CHECK(fifo.droppedBlocks() == 0);
}

TEST_CASE(
    "audio FIFO push/pop throughput at audio block sizes",
    "[.benchmark][audio][fifo][performance]")
{
    // Capacity sized for a typical tool analysis window (65536 samples).
    // Block sizes mirror common audio callback sizes in practice.
    constexpr std::size_t fifoCapacity = 65536;
    constexpr std::size_t blockSize128 = 128;
    constexpr std::size_t blockSize256 = 256;
    constexpr std::size_t blockSize512 = 512;

    std::array<float, blockSize512> block{};
    block.fill(0.5f);

    std::array<float, blockSize512> readBuf{};

    AudioSampleFifo<fifoCapacity> fifo128;
    AudioSampleFifo<fifoCapacity> fifo256;
    AudioSampleFifo<fifoCapacity> fifo512;

    // Each body is a matched pair -- the operation being measured, and the
    // opposite operation that keeps the FIFO from filling or emptying. The
    // setup is inside the timed region, so an absolute number here is the cost
    // of a push *and* a pop; the names are kept as they are because the
    // performance history in docs/development/quality/run-history/ is keyed on
    // them, and renaming would orphan the trend rather than correct it.
    //
    // What each pair must not do is measure the wrong branch. A rejected push
    // returns early without copying anything, and a pop from an empty FIFO
    // copies nothing either -- both are far cheaper than the accepted path, and
    // a benchmark that drifted into them would report a FIFO that had merely
    // stopped doing the work. Nothing asserted that until now.
    //
    // The two checks after each block are what assert it, and they sit outside
    // the timed region so they cost the measurement nothing:
    //
    //   droppedBlocks() == 0     No push was ever rejected -- exact, because
    //                            push increments that counter on precisely the
    //                            branch this must not measure. This covers the
    //                            measured push above and the setup push below.
    //
    //   available() <= blockSize The pops kept up. A pop that returned short
    //                            would leave its remainder buffered, so a
    //                            systematic shortfall shows as a backlog and,
    //                            once the backlog reaches capacity, as drops.
    //
    // Together they are also what proves a measured pop was never short: a push
    // accepted immediately beforehand leaves at least one block available, and
    // pop returns min(requested, available).
    //
    // The setup return is discarded rather than accumulated, which is worth
    // justifying because the obvious reading is that it should be summed and
    // compared. It was written that way first. The accumulator has to live
    // inside the lambda, which is the timed region, and these numbers feed the
    // performance trend -- paying measurement accuracy for a check that the
    // counters already provide for free is the wrong trade. A per-iteration sum
    // would additionally catch a single isolated short pop, but an isolated
    // short pop does not change what the benchmark measures; systematic drift
    // does, and that is what the invariants below catch.

    BENCHMARK("push 128-sample block")
    {
        static_cast<void>(fifo128.pop(readBuf.data(), blockSize128));
        return fifo128.push(block.data(), blockSize128);
    };

    CHECK(fifo128.droppedBlocks() == 0);
    CHECK(fifo128.available() <= blockSize128);

    BENCHMARK("pop 128-sample block")
    {
        static_cast<void>(fifo128.push(block.data(), blockSize128));
        return fifo128.pop(readBuf.data(), blockSize128);
    };

    CHECK(fifo128.droppedBlocks() == 0);
    CHECK(fifo128.available() <= blockSize128);

    BENCHMARK("push 256-sample block")
    {
        static_cast<void>(fifo256.pop(readBuf.data(), blockSize256));
        return fifo256.push(block.data(), blockSize256);
    };

    CHECK(fifo256.droppedBlocks() == 0);
    CHECK(fifo256.available() <= blockSize256);

    BENCHMARK("pop 256-sample block")
    {
        static_cast<void>(fifo256.push(block.data(), blockSize256));
        return fifo256.pop(readBuf.data(), blockSize256);
    };

    CHECK(fifo256.droppedBlocks() == 0);
    CHECK(fifo256.available() <= blockSize256);

    BENCHMARK("push 512-sample block")
    {
        static_cast<void>(fifo512.pop(readBuf.data(), blockSize512));
        return fifo512.push(block.data(), blockSize512);
    };

    CHECK(fifo512.droppedBlocks() == 0);
    CHECK(fifo512.available() <= blockSize512);

    BENCHMARK("pop 512-sample block")
    {
        static_cast<void>(fifo512.push(block.data(), blockSize512));
        return fifo512.pop(readBuf.data(), blockSize512);
    };

    CHECK(fifo512.droppedBlocks() == 0);
    CHECK(fifo512.available() <= blockSize512);
}
