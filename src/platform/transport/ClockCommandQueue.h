#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "../audio/RealtimeSafety.h"
#include "../score/MusicalTime.h"

// What the message thread asks of the clock's renderer, and the single-producer,
// single-consumer queue it travels in. No JUCE dependency.
//
// A queue rather than a set of atomics, because a command is several values --
// a seek's tick, a play's count-in -- that must arrive together, and because
// two commands in one display tick (stop, then play) must both happen, in
// order. The message thread pushes; the audio thread drains at the top of each
// block. Neither waits: a full queue refuses the push, and the message thread
// sends it again on its next tick.
namespace transport
{
struct ClockCommand
{
    enum class Kind : std::uint8_t
    {
        // Start from the current position, after `countInBars` bars of count-in.
        play,
        // Hold the position.
        pause,
        // Hold, and go back to the start of the loop, or of the score.
        stop,
        // Move to `tick`.
        seek
    };

    Kind kind = Kind::pause;
    score::Tick tick = 0;
    int countInBars = 0;
};

// Single producer, single consumer, of any trivially copyable item. Used for
// commands (message thread to audio thread) and for the blocks a take records
// (audio thread to message thread), so both ends are annotated non-blocking.
template <typename Item, std::size_t Capacity> class SpscQueue
{
    static_assert(std::is_trivially_copyable_v<Item>);
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "a power of two");
    static_assert(std::atomic<std::size_t>::is_always_lock_free);

  public:
    // The producer. False when full; nothing is written.
    [[nodiscard]] bool push(const Item& item) noexcept PRACTICE_TAKES_NONBLOCKING
    {
        const std::size_t tail = writeIndex.load(std::memory_order_relaxed);
        const std::size_t head = readIndex.load(std::memory_order_acquire);
        if (tail - head == Capacity)
        {
            return false;
        }
        slots[tail & (Capacity - 1)] = item;
        writeIndex.store(tail + 1, std::memory_order_release);
        return true;
    }

    // The producer: whether a push now would succeed.
    [[nodiscard]] bool hasSpace() const noexcept PRACTICE_TAKES_NONBLOCKING
    {
        return writeIndex.load(std::memory_order_relaxed) -
                   readIndex.load(std::memory_order_acquire) <
               Capacity;
    }

    // The consumer. False when empty.
    [[nodiscard]] bool pop(Item& item) noexcept PRACTICE_TAKES_NONBLOCKING
    {
        const std::size_t head = readIndex.load(std::memory_order_relaxed);
        const std::size_t tail = writeIndex.load(std::memory_order_acquire);
        if (head == tail)
        {
            return false;
        }
        item = slots[head & (Capacity - 1)];
        readIndex.store(head + 1, std::memory_order_release);
        return true;
    }

  private:
    std::array<Item, Capacity> slots{};
    std::atomic<std::size_t> writeIndex{0};
    std::atomic<std::size_t> readIndex{0};
};
} // namespace transport
