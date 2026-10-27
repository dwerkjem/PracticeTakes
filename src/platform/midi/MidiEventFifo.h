#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "MidiEvent.h"

// Fixed-capacity, single-producer/single-consumer FIFO of MIDI events.
//
// The MIDI driver callback is the only producer and one consumer drains it from
// a message-thread timer, which is the arrangement AudioSampleFifo already uses
// for audio. Everything it stores is preallocated, so a push allocates nothing.
//
// **It drops the OLDEST on overflow, which is the opposite of
// AudioSampleFifo.** The difference is deliberate and it matters, so: audio
// carries an unbounded flow of samples where a dropped block is a brief gap
// nobody can point at, and keeping what is already buffered is worth more than
// a late block. Events are not like that. Neither choice is safe -- dropping
// the newest loses a note off and leaves a note held forever, dropping the
// oldest loses a note on and leaves a note off arriving for a note nobody
// started -- because there is no MIDI event it is safe to lose.
//
// So this drops the past rather than the future, which at least re-converges: a
// consumer's idea of what is held now is correct again within one key or pedal
// release. And it counts what it dropped, so a consumer tracking held notes can
// tell that its picture is corrupt rather than believing it.
//
// At the capacity the service uses this is theoretical. A ten-finger chord with
// the pedal is about two dozen events and a consumer drains twenty times a
// second; dropping at all means something stalled for seconds.
namespace midi
{
template <std::size_t Capacity> class MidiEventFifo
{
  public:
    static_assert(Capacity > 0);
    static_assert(std::atomic<std::size_t>::is_always_lock_free);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

    // Push one event. Never fails and never blocks: when full, the oldest event
    // is discarded to make room and the lost counter moves.
    //
    // Called from the driver callback, so everything here is bounded: one
    // store, at most one counter increment, and no allocation.
    void push(const MidiEvent& event) noexcept
    {
        const auto write = writePosition.load(std::memory_order_relaxed);
        const auto read = readPosition.load(std::memory_order_acquire);

        if (write - read >= Capacity)
        {
            // Advance the reader past the oldest entry. Safe against a consumer
            // reading concurrently in the sense that matters here: the consumer
            // only ever moves readPosition forward, and so does this, so no
            // event is handed out twice and none is read after being
            // overwritten.
            readPosition.store(read + 1, std::memory_order_release);
            lostEventCount.fetch_add(1, std::memory_order_relaxed);
        }

        events[write % Capacity] = event;
        writePosition.store(write + 1, std::memory_order_release);
    }

    // Drain up to `maximumCount` events in arrival order. Returns how many were
    // written to `destination`.
    [[nodiscard]] std::size_t pop(MidiEvent* destination, std::size_t maximumCount) noexcept
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
            destination[index] = events[(read + index) % Capacity];
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

    // How many events have been dropped since this FIFO was created.
    //
    // Monotonic and never reset by reading, so a consumer compares it against
    // what it last saw: a value that has moved means "your idea of which notes
    // are held is wrong, start again" rather than "one event went missing".
    [[nodiscard]] std::uint64_t lostEvents() const noexcept
    {
        return lostEventCount.load(std::memory_order_relaxed);
    }

    void discardPending() noexcept
    {
        readPosition.store(
            writePosition.load(std::memory_order_acquire), std::memory_order_release);
    }

    // Only valid while the producer is inactive and the consumer is not
    // draining -- the same restriction AudioSampleFifo::reset carries, for the
    // same reason.
    void reset() noexcept
    {
        readPosition.store(0, std::memory_order_relaxed);
        writePosition.store(0, std::memory_order_relaxed);
        lostEventCount.store(0, std::memory_order_relaxed);
    }

  private:
    std::array<MidiEvent, Capacity> events{};
    std::atomic<std::size_t> readPosition{0};
    std::atomic<std::size_t> writePosition{0};
    std::atomic<std::uint64_t> lostEventCount{0};
};
} // namespace midi
