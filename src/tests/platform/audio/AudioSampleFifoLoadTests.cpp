#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "platform/audio/AudioSampleFifo.h"

// Load and soak tests for the shared capture FIFO.
//
// Tagged `[.load]`, so `ctest` and a plain test run skip them: they are slow by
// construction. The tag is deliberately not `[.benchmark]` -- a benchmark
// answers "how fast is one call" and is meant to be compared over time, whereas
// these answer "does this survive pressure" and are pass/fail. Sharing a tag
// would mean neither could be run without the other.
//
// NECESSARY BUT NOT SUFFICIENT. These tests exercise real concurrency, but a
// green run is *not* proof that AudioSampleFifo is correct. A data race that
// happens not to manifest on this machine, on this scheduler, in this run, will
// pass silently. Real verification needs a ThreadSanitizer build, which
// docs/development/QA_STRATEGY.md area 13 tracks and which is deliberately out
// of scope here. Do not read a pass as "the FIFO is proven".
//
// What these *can* prove is the observable contract: every sample that was
// accepted comes back exactly once, in order, and an overflow is reported
// rather than corrupting the buffer.

namespace
{
constexpr std::size_t fifoCapacity = 65536;
using Fifo = AudioSampleFifo<fifoCapacity>;

// The audio callback's block size. 512 frames at 48 kHz is a typical setting.
constexpr std::size_t blockSize = 512;

// Each sample carries its own index, so the consumer can assert both ordering
// and exactly-once delivery from the values alone.
[[nodiscard]] float expectedValue(std::size_t index)
{
    return static_cast<float>(index);
}

// How long the soak test runs. Configurable so the same test serves a quick
// local run and a longer scheduled one; CI runs the short default purely to
// prove the harness works.
[[nodiscard]] std::chrono::milliseconds soakDuration()
{
    if (const char* configured = std::getenv("PRACTICE_TAKES_SOAK_MILLISECONDS"))
    {
        if (const auto parsed = std::atoi(configured); parsed > 0)
        {
            return std::chrono::milliseconds{parsed};
        }
    }

    return std::chrono::milliseconds{1500};
}

void report(const std::string& what)
{
    // Printed rather than asserted: a passing load test should still show
    // whether headroom is shrinking.
    std::cout << "    [load] " << what << '\n';
}
} // namespace

TEST_CASE("every accepted sample is delivered exactly once and in order", "[.load][fifo]")
{
    // The producer retries on a full ring rather than dropping, so nothing is
    // legitimately lost and the consumer must see the complete sequence.
    constexpr std::size_t totalSamples = blockSize * 4000;

    Fifo fifo;
    std::atomic<bool> producerDone{false};

    std::thread producer(
        [&]
        {
            std::vector<float> block(blockSize);

            for (std::size_t offset = 0; offset < totalSamples; offset += blockSize)
            {
                for (std::size_t index = 0; index < blockSize; ++index)
                {
                    block[index] = expectedValue(offset + index);
                }

                while (!fifo.push(block.data(), blockSize))
                {
                    std::this_thread::yield();
                }
            }

            producerDone.store(true, std::memory_order_release);
        });

    std::size_t received = 0;
    std::size_t mismatches = 0;
    std::vector<float> drained(blockSize);

    while (received < totalSamples)
    {
        const auto count = fifo.pop(drained.data(), drained.size());

        for (std::size_t index = 0; index < count; ++index)
        {
            if (drained[index] != expectedValue(received + index))
            {
                ++mismatches;
            }
        }

        received += count;

        if (count == 0 && producerDone.load(std::memory_order_acquire) && fifo.available() == 0)
        {
            break;
        }
    }

    producer.join();

    CHECK(received == totalSamples);
    CHECK(mismatches == 0);
    CHECK(fifo.droppedBlocks() == 0);

    report(
        "exactly-once: " + std::to_string(received) + " samples, " + std::to_string(mismatches) +
        " mismatches");
}

TEST_CASE(
    "a producer outrunning its consumer reports drops rather than corrupting",
    "[.load][fifo]")
{
    // The documented behaviour is that a block which does not fit is dropped
    // whole and counted, leaving the buffered samples in order. The failure
    // this guards against is a partial write that tears the stream.
    Fifo fifo;

    std::vector<float> block(blockSize);
    std::size_t pushed = 0;
    std::size_t attempted = 0;

    // Fill well past capacity with the consumer stalled.
    while (attempted < fifoCapacity * 3 / blockSize)
    {
        for (std::size_t index = 0; index < blockSize; ++index)
        {
            block[index] = expectedValue(pushed + index);
        }

        if (fifo.push(block.data(), blockSize))
        {
            pushed += blockSize;
        }

        ++attempted;
    }

    CHECK(fifo.droppedBlocks() > 0);
    CHECK(fifo.droppedSamples() >= fifo.droppedBlocks());

    // Everything that *was* accepted must still be intact and in order.
    std::vector<float> drained(blockSize);
    std::size_t verified = 0;
    std::size_t mismatches = 0;

    while (true)
    {
        const auto count = fifo.pop(drained.data(), drained.size());

        if (count == 0)
        {
            break;
        }

        for (std::size_t index = 0; index < count; ++index)
        {
            if (drained[index] != expectedValue(verified + index))
            {
                ++mismatches;
            }
        }

        verified += count;
    }

    CHECK(verified == pushed);
    CHECK(mismatches == 0);

    report(
        "overflow: " + std::to_string(fifo.droppedBlocks()) + " blocks dropped, " +
        std::to_string(verified) + " samples intact");
}

TEST_CASE("many simultaneous consumers each receive their own complete stream", "[.load][fifo]")
{
    // One preallocated FIFO per active tool consumer is the documented shape of
    // the capture path, so this is what "several tools open at once" costs.
    //
    // The count here is provisional. The audio-thread contract does not state a
    // maximum number of simultaneous consumers, and that number is still an
    // open question on the rebuild-test-suite change. Eight is the current tool
    // count plus headroom, not an agreed cap.
    constexpr std::size_t consumerCount = 8;
    constexpr std::size_t samplesPerConsumer = blockSize * 500;

    std::vector<std::unique_ptr<Fifo>> fifos;
    fifos.reserve(consumerCount);

    for (std::size_t index = 0; index < consumerCount; ++index)
    {
        fifos.push_back(std::make_unique<Fifo>());
    }

    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    std::vector<std::size_t> receivedCounts(consumerCount, 0);
    std::vector<std::size_t> mismatchCounts(consumerCount, 0);

    for (std::size_t consumer = 0; consumer < consumerCount; ++consumer)
    {
        producers.emplace_back(
            [&, consumer]
            {
                std::vector<float> block(blockSize);

                for (std::size_t offset = 0; offset < samplesPerConsumer; offset += blockSize)
                {
                    for (std::size_t index = 0; index < blockSize; ++index)
                    {
                        block[index] = expectedValue(offset + index);
                    }

                    while (!fifos[consumer]->push(block.data(), blockSize))
                    {
                        std::this_thread::yield();
                    }
                }
            });

        consumers.emplace_back(
            [&, consumer]
            {
                std::vector<float> drained(blockSize);
                std::size_t received = 0;

                while (received < samplesPerConsumer)
                {
                    const auto count = fifos[consumer]->pop(drained.data(), drained.size());

                    for (std::size_t index = 0; index < count; ++index)
                    {
                        if (drained[index] != expectedValue(received + index))
                        {
                            ++mismatchCounts[consumer];
                        }
                    }

                    received += count;

                    if (count == 0)
                    {
                        std::this_thread::yield();
                    }
                }

                receivedCounts[consumer] = received;
            });
    }

    for (auto& thread : producers)
    {
        thread.join();
    }

    for (auto& thread : consumers)
    {
        thread.join();
    }

    for (std::size_t consumer = 0; consumer < consumerCount; ++consumer)
    {
        INFO("consumer " << consumer);
        CHECK(receivedCounts[consumer] == samplesPerConsumer);
        CHECK(mismatchCounts[consumer] == 0);
        CHECK(fifos[consumer]->droppedBlocks() == 0);
    }

    report(
        "saturation: " + std::to_string(consumerCount) + " concurrent consumers, " +
        std::to_string(consumerCount * samplesPerConsumer) + " samples total");
}

TEST_CASE("a stalled consumer overflows without affecting the others", "[.load][fifo]")
{
    // A slow or hung tool must not be able to starve or corrupt another tool's
    // stream -- that isolation is the whole reason each consumer has its own
    // FIFO rather than sharing one.
    constexpr std::size_t samples = blockSize * 400;

    Fifo healthy;
    Fifo stalled;

    std::thread producer(
        [&]
        {
            std::vector<float> block(blockSize);

            for (std::size_t offset = 0; offset < samples; offset += blockSize)
            {
                for (std::size_t index = 0; index < blockSize; ++index)
                {
                    block[index] = expectedValue(offset + index);
                }

                while (!healthy.push(block.data(), blockSize))
                {
                    std::this_thread::yield();
                }

                // The stalled consumer's FIFO is written to and never drained.
                (void)stalled.push(block.data(), blockSize);
            }
        });

    std::vector<float> drained(blockSize);
    std::size_t received = 0;
    std::size_t mismatches = 0;

    while (received < samples)
    {
        const auto count = healthy.pop(drained.data(), drained.size());

        for (std::size_t index = 0; index < count; ++index)
        {
            if (drained[index] != expectedValue(received + index))
            {
                ++mismatches;
            }
        }

        received += count;

        if (count == 0)
        {
            std::this_thread::yield();
        }
    }

    producer.join();

    CHECK(received == samples);
    CHECK(mismatches == 0);
    CHECK(healthy.droppedBlocks() == 0);

    // The stalled one overflowed, which is the documented behaviour.
    CHECK(stalled.droppedBlocks() > 0);

    report(
        "isolation: healthy consumer lost 0 samples while the stalled one dropped " +
        std::to_string(stalled.droppedBlocks()) + " blocks");
}

TEST_CASE("sustained operation stays bounded", "[.load][fifo][soak]")
{
    // Runs continuously for a configurable duration. Its job is to surface
    // drift and unbounded growth, which a short functional test cannot.
    //
    // The FIFO is fixed-capacity and allocation-free by construction, so there
    // is no heap growth to measure here; what is asserted is that throughput
    // does not collapse and the stream never tears over a long run.
    const auto duration = soakDuration();

    Fifo fifo;
    std::atomic<bool> stop{false};
    std::atomic<std::size_t> producedSamples{0};

    std::thread producer(
        [&]
        {
            std::vector<float> block(blockSize);
            std::size_t offset = 0;

            while (!stop.load(std::memory_order_acquire))
            {
                for (std::size_t index = 0; index < blockSize; ++index)
                {
                    block[index] = expectedValue(offset + index);
                }

                if (fifo.push(block.data(), blockSize))
                {
                    offset += blockSize;
                    producedSamples.store(offset, std::memory_order_release);
                }
                else
                {
                    std::this_thread::yield();
                }
            }
        });

    const auto started = std::chrono::steady_clock::now();

    std::vector<float> drained(blockSize);
    std::size_t received = 0;
    std::size_t mismatches = 0;

    while (std::chrono::steady_clock::now() - started < duration)
    {
        const auto count = fifo.pop(drained.data(), drained.size());

        for (std::size_t index = 0; index < count; ++index)
        {
            if (drained[index] != expectedValue(received + index))
            {
                ++mismatches;
            }
        }

        received += count;

        if (count == 0)
        {
            std::this_thread::yield();
        }
    }

    stop.store(true, std::memory_order_release);
    producer.join();

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);

    CHECK(mismatches == 0);
    CHECK(received > 0);

    // Never more than the ring can hold, or the consumer has fallen behind in a
    // way the fixed capacity should have made impossible.
    CHECK(fifo.available() <= fifoCapacity);

    const auto samplesPerSecond =
        elapsed.count() > 0 ? received * 1000 / static_cast<std::size_t>(elapsed.count()) : 0;

    report(
        "soak: " + std::to_string(elapsed.count()) + " ms, " + std::to_string(received) +
        " samples, " + std::to_string(samplesPerSecond) + " samples/s, " +
        std::to_string(mismatches) + " mismatches");

    // At 48 kHz a real capture delivers 48,000 samples a second. Anything near
    // that low here means the harness is not actually exercising the FIFO.
    CHECK(samplesPerSecond > 48000);
}
