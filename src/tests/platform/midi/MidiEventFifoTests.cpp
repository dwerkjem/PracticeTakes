#include <catch2/catch_test_macros.hpp>

#include "platform/midi/MidiEventFifo.h"

#include <array>
#include <vector>

using namespace midi;

namespace
{
constexpr std::size_t capacity = 8;

[[nodiscard]] MidiEvent noteOn(int noteNumber)
{
    MidiEvent event;
    event.kind = MidiEventKind::noteOn;
    event.noteNumber = noteNumber;
    event.velocity = 100;

    return event;
}

// Drain everything currently buffered.
[[nodiscard]] std::vector<MidiEvent> drain(MidiEventFifo<capacity>& fifo)
{
    std::array<MidiEvent, capacity * 4> buffer{};
    const std::size_t count = fifo.pop(buffer.data(), buffer.size());

    return {buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(count)};
}
} // namespace

TEST_CASE("a new event fifo is empty and has lost nothing", "[midi][fifo]")
{
    MidiEventFifo<capacity> fifo;

    CHECK(fifo.available() == 0);
    CHECK(fifo.lostEvents() == 0);
    CHECK(drain(fifo).empty());
}

TEST_CASE("events come out in the order they went in", "[midi][fifo]")
{
    MidiEventFifo<capacity> fifo;

    for (int note = 60; note < 65; ++note)
    {
        fifo.push(noteOn(note));
    }

    REQUIRE(fifo.available() == 5);

    const std::vector<MidiEvent> drained = drain(fifo);

    REQUIRE(drained.size() == 5);

    for (std::size_t index = 0; index < drained.size(); ++index)
    {
        CHECK(drained[index].noteNumber == 60 + static_cast<int>(index));
    }

    CHECK(fifo.available() == 0);
    CHECK(fifo.lostEvents() == 0);
}

TEST_CASE("filling to capacity loses nothing", "[midi][fifo]")
{
    MidiEventFifo<capacity> fifo;

    for (std::size_t index = 0; index < capacity; ++index)
    {
        fifo.push(noteOn(static_cast<int>(index)));
    }

    CHECK(fifo.available() == capacity);
    CHECK(fifo.lostEvents() == 0);
}

// The decision this type exists for, and the one that differs from
// AudioSampleFifo: the past is dropped rather than the future, so a consumer's
// idea of what is held now re-converges within one key or pedal release.
TEST_CASE("overflow drops the oldest event", "[midi][fifo]")
{
    MidiEventFifo<capacity> fifo;

    for (std::size_t index = 0; index < capacity + 3; ++index)
    {
        fifo.push(noteOn(static_cast<int>(index)));
    }

    CHECK(fifo.available() == capacity);
    CHECK(fifo.lostEvents() == 3);

    const std::vector<MidiEvent> drained = drain(fifo);

    REQUIRE(drained.size() == capacity);

    // The three oldest went; the newest are all there, in order.
    CHECK(drained.front().noteNumber == 3);
    CHECK(drained.back().noteNumber == static_cast<int>(capacity) + 2);
}

TEST_CASE("the lost counter is monotonic and reading it does not clear it", "[midi][fifo]")
{
    MidiEventFifo<capacity> fifo;

    for (std::size_t index = 0; index < capacity + 1; ++index)
    {
        fifo.push(noteOn(static_cast<int>(index)));
    }

    REQUIRE(fifo.lostEvents() == 1);
    CHECK(fifo.lostEvents() == 1);

    // Draining does not forget what was lost either: a consumer compares the
    // value against what it last saw, so it has to survive a drain.
    static_cast<void>(drain(fifo));
    CHECK(fifo.lostEvents() == 1);

    for (std::size_t index = 0; index < capacity + 2; ++index)
    {
        fifo.push(noteOn(static_cast<int>(index)));
    }

    CHECK(fifo.lostEvents() == 3);
}

TEST_CASE("a partial drain leaves the rest in order", "[midi][fifo]")
{
    MidiEventFifo<capacity> fifo;

    for (int note = 0; note < 5; ++note)
    {
        fifo.push(noteOn(note));
    }

    std::array<MidiEvent, 2> buffer{};
    REQUIRE(fifo.pop(buffer.data(), buffer.size()) == 2);
    CHECK(buffer[0].noteNumber == 0);
    CHECK(buffer[1].noteNumber == 1);

    const std::vector<MidiEvent> rest = drain(fifo);

    REQUIRE(rest.size() == 3);
    CHECK(rest.front().noteNumber == 2);
    CHECK(rest.back().noteNumber == 4);
}

TEST_CASE("the fifo wraps without losing order", "[midi][fifo]")
{
    MidiEventFifo<capacity> fifo;

    // Push and drain more than capacity in total, a few at a time, so the
    // indices wrap the ring several times.
    int expected = 0;

    for (int round = 0; round < 10; ++round)
    {
        for (int index = 0; index < 3; ++index)
        {
            fifo.push(noteOn(expected + index));
        }

        const std::vector<MidiEvent> drained = drain(fifo);

        REQUIRE(drained.size() == 3);

        for (std::size_t index = 0; index < drained.size(); ++index)
        {
            CHECK(drained[index].noteNumber == expected + static_cast<int>(index));
        }

        expected += 3;
    }

    CHECK(fifo.lostEvents() == 0);
}

TEST_CASE("pending events can be discarded", "[midi][fifo]")
{
    MidiEventFifo<capacity> fifo;
    fifo.push(noteOn(60));
    fifo.push(noteOn(62));

    fifo.discardPending();

    CHECK(fifo.available() == 0);
    CHECK(drain(fifo).empty());
}

TEST_CASE("reset clears the buffer and the counter", "[midi][fifo]")
{
    MidiEventFifo<capacity> fifo;

    for (std::size_t index = 0; index < capacity + 2; ++index)
    {
        fifo.push(noteOn(static_cast<int>(index)));
    }

    REQUIRE(fifo.lostEvents() == 2);

    fifo.reset();

    CHECK(fifo.available() == 0);
    CHECK(fifo.lostEvents() == 0);
}

TEST_CASE("popping into nothing is harmless", "[midi][fifo]")
{
    MidiEventFifo<capacity> fifo;
    fifo.push(noteOn(60));

    CHECK(fifo.pop(nullptr, 4) == 0);

    std::array<MidiEvent, 1> buffer{};
    CHECK(fifo.pop(buffer.data(), 0) == 0);

    // Neither consumed the event.
    CHECK(fifo.available() == 1);
}
