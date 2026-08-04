#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

// Fixed-capacity, single-producer/single-consumer audio FIFO.
//
// The audio callback is the only producer and one analysis tool is the only
// consumer. When a complete callback block does not fit, the newest block is
// dropped and the already-buffered samples remain in order.
template <std::size_t Capacity> class AudioSampleFifo
{
  public:
    static_assert(Capacity > 0);
    static_assert(std::atomic<std::size_t>::is_always_lock_free);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

    [[nodiscard]] bool push(const float* source, std::size_t count, float gain = 1.0f) noexcept
    {
        if (source == nullptr || count == 0)
        {
            return true;
        }

        const auto write = writePosition.load(std::memory_order_relaxed);
        const auto read = readPosition.load(std::memory_order_acquire);
        if (count > Capacity - (write - read))
        {
            droppedBlockCount.fetch_add(1, std::memory_order_relaxed);
            droppedSampleCount.fetch_add(
                static_cast<std::uint64_t>(count), std::memory_order_relaxed);
            return false;
        }

        for (std::size_t index = 0; index < count; ++index)
        {
            samples[(write + index) % Capacity] = source[index] * gain;
        }

        writePosition.store(write + count, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::size_t pop(float* destination, std::size_t maximumCount) noexcept
    {
        if (destination == nullptr || maximumCount == 0)
        {
            return 0;
        }

        const auto read = readPosition.load(std::memory_order_relaxed);
        const auto write = writePosition.load(std::memory_order_acquire);
        const auto count = std::min(maximumCount, write - read);

        for (std::size_t index = 0; index < count; ++index)
        {
            destination[index] = samples[(read + index) % Capacity];
        }

        readPosition.store(read + count, std::memory_order_release);
        return count;
    }

    [[nodiscard]] std::size_t available() const noexcept
    {
        const auto write = writePosition.load(std::memory_order_acquire);
        const auto read = readPosition.load(std::memory_order_relaxed);
        return write - read;
    }

    void discardPending() noexcept
    {
        readPosition.store(
            writePosition.load(std::memory_order_acquire), std::memory_order_release);
    }

    // Reset is only valid while the producer is inactive and the consumer is
    // detached. Runtime format changes use discardPending instead.
    void reset() noexcept
    {
        readPosition.store(0, std::memory_order_relaxed);
        writePosition.store(0, std::memory_order_relaxed);
        droppedBlockCount.store(0, std::memory_order_relaxed);
        droppedSampleCount.store(0, std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t droppedBlocks() const noexcept
    {
        return droppedBlockCount.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t droppedSamples() const noexcept
    {
        return droppedSampleCount.load(std::memory_order_relaxed);
    }

  private:
    std::array<float, Capacity> samples{};
    std::atomic<std::size_t> readPosition{0};
    std::atomic<std::size_t> writePosition{0};
    std::atomic<std::uint64_t> droppedBlockCount{0};
    std::atomic<std::uint64_t> droppedSampleCount{0};
};
