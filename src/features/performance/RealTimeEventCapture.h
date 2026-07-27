#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace performance
{
struct AudioCallbackEvent
{
    std::uint64_t sequence = 0;
    std::uint64_t startedAtNanoseconds = 0;
    std::uint64_t durationNanoseconds = 0;
    std::uint64_t deadlineNanoseconds = 0;
    std::uint32_t dropoutCount = 0;
    std::uint32_t underrunCount = 0;
    bool deadlineMissed = false;
};

template <std::size_t Capacity> class RealTimeEventCapture
{
  public:
    static_assert(Capacity > 0);
    static_assert(std::is_trivially_copyable_v<AudioCallbackEvent>);
    static_assert(std::atomic<std::size_t>::is_always_lock_free);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

    [[nodiscard]] bool record(
        std::uint64_t startedAtNanoseconds,
        std::uint64_t durationNanoseconds,
        std::uint64_t deadlineNanoseconds,
        std::uint32_t dropoutCount = 0,
        std::uint32_t underrunCount = 0) noexcept
    {
        const auto write = writePosition.load(std::memory_order_relaxed);
        const auto read = readPosition.load(std::memory_order_acquire);
        if (write - read == Capacity)
        {
            droppedEventCount.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        events[write % Capacity] = {
            .sequence = write,
            .startedAtNanoseconds = startedAtNanoseconds,
            .durationNanoseconds = durationNanoseconds,
            .deadlineNanoseconds = deadlineNanoseconds,
            .dropoutCount = dropoutCount,
            .underrunCount = underrunCount,
            .deadlineMissed = durationNanoseconds > deadlineNanoseconds};
        writePosition.store(write + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::size_t drain(std::span<AudioCallbackEvent> destination) noexcept
    {
        const auto read = readPosition.load(std::memory_order_relaxed);
        const auto write = writePosition.load(std::memory_order_acquire);
        const auto count = std::min(destination.size(), write - read);
        for (std::size_t index = 0; index < count; ++index)
            destination[index] = events[(read + index) % Capacity];
        readPosition.store(read + count, std::memory_order_release);
        return count;
    }

    [[nodiscard]] std::size_t available() const noexcept
    {
        const auto write = writePosition.load(std::memory_order_acquire);
        const auto read = readPosition.load(std::memory_order_relaxed);
        return write - read;
    }

    [[nodiscard]] std::uint64_t droppedEvents() const noexcept
    {
        return droppedEventCount.load(std::memory_order_relaxed);
    }

    void reset() noexcept
    {
        readPosition.store(0, std::memory_order_relaxed);
        writePosition.store(0, std::memory_order_relaxed);
        droppedEventCount.store(0, std::memory_order_relaxed);
    }

  private:
    std::array<AudioCallbackEvent, Capacity> events{};
    std::atomic<std::size_t> readPosition{0};
    std::atomic<std::size_t> writePosition{0};
    std::atomic<std::uint64_t> droppedEventCount{0};
};
} // namespace performance