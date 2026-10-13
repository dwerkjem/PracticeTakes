#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "platform/midi/MidiMessageTranslation.h"

#include <array>
#include <cstdint>

using namespace midi;

namespace
{
// A message as a keyboard would put it on the wire.
[[nodiscard]] std::array<std::uint8_t, 3>
message(std::uint8_t status, std::uint8_t first, std::uint8_t second)
{
    return {status, first, second};
}

[[nodiscard]] std::optional<MidiEvent> translateOf(
    std::uint8_t status,
    std::uint8_t first,
    std::uint8_t second,
    double deviceTimestamp = 0.0,
    double receivedTimestamp = 0.0)
{
    const auto bytes = message(status, first, second);

    return translate(bytes.data(), 3, deviceTimestamp, receivedTimestamp);
}
} // namespace

TEST_CASE("a note on becomes a note on", "[midi][translation]")
{
    const auto event = translateOf(0x90, 60, 100);

    REQUIRE(event.has_value());
    CHECK(event->kind == MidiEventKind::noteOn);
    CHECK(event->noteNumber == 60);
    CHECK(event->velocity == 100);
    CHECK(event->channel == 1);
}

TEST_CASE("a note off becomes a note off", "[midi][translation]")
{
    const auto event = translateOf(0x80, 60, 0);

    REQUIRE(event.has_value());
    CHECK(event->kind == MidiEventKind::noteOff);
    CHECK(event->noteNumber == 60);
}

// The single most common cause of a note stuck on forever in a MIDI consumer.
// Running status lets a keyboard send a whole phrase under one note-on status
// byte, releasing keys with velocity zero, and a great many real keyboards do.
TEST_CASE("a note on with velocity zero is a note off", "[midi][translation]")
{
    const auto event = translateOf(0x90, 60, 0);

    REQUIRE(event.has_value());
    CHECK(event->kind == MidiEventKind::noteOff);
    CHECK(event->noteNumber == 60);
    CHECK(event->velocity == 0);
}

TEST_CASE("a note off carries its release velocity", "[midi][translation]")
{
    const auto event = translateOf(0x80, 64, 42);

    REQUIRE(event.has_value());
    CHECK(event->kind == MidiEventKind::noteOff);
    CHECK(event->velocity == 42);
}

TEST_CASE("the channel is reported as one to sixteen", "[midi][translation]")
{
    // The wire numbers channels 0-15; every document a reader will consult
    // numbers them 1-16. Converted once, in translation.
    CHECK(translateOf(0x90, 60, 100)->channel == 1);
    CHECK(translateOf(0x91, 60, 100)->channel == 2);
    CHECK(translateOf(0x9F, 60, 100)->channel == 16);
    CHECK(translateOf(0x8F, 60, 0)->channel == 16);
    CHECK(translateOf(0xBF, 64, 127)->channel == 16);
}

TEST_CASE("the full note and velocity range translates", "[midi][translation]")
{
    for (int note = 0; note <= 127; ++note)
    {
        const auto event =
            translateOf(0x90, static_cast<std::uint8_t>(note), static_cast<std::uint8_t>(1));

        REQUIRE(event.has_value());
        CHECK(event->noteNumber == note);
    }

    for (int velocity = 1; velocity <= 127; ++velocity)
    {
        const auto event = translateOf(0x90, 60, static_cast<std::uint8_t>(velocity));

        REQUIRE(event.has_value());
        CHECK(event->kind == MidiEventKind::noteOn);
        CHECK(event->velocity == velocity);
    }
}

TEST_CASE("the sustain pedal crosses at sixty-four", "[midi][translation]")
{
    // Written as `== 127` surprisingly often, which is wrong for every keyboard
    // that half-pedals: a pedal held at 100 is a pedal that is down.
    SECTION("below the threshold is up")
    {
        CHECK_FALSE(translateOf(0xB0, 64, 0)->pedalDown);
        CHECK_FALSE(translateOf(0xB0, 64, 63)->pedalDown);
    }

    SECTION("at and above the threshold is down")
    {
        CHECK(translateOf(0xB0, 64, 64)->pedalDown);
        CHECK(translateOf(0xB0, 64, 65)->pedalDown);
        CHECK(translateOf(0xB0, 64, 100)->pedalDown);
        CHECK(translateOf(0xB0, 64, 127)->pedalDown);
    }

    SECTION("it is a pedal event, and carries the raw value")
    {
        const auto event = translateOf(0xB0, 64, 100);

        REQUIRE(event.has_value());
        CHECK(event->kind == MidiEventKind::sustainPedal);
        CHECK(event->velocity == 100);
    }
}

TEST_CASE("other controllers are ignored", "[midi][translation]")
{
    // Dropping these is not an error: most of what a keyboard sends is not
    // modelled here, and a capture path that treated the ordinary as
    // exceptional would be wrong about every real device.
    CHECK_FALSE(translateOf(0xB0, 1, 100).has_value());  // modulation
    CHECK_FALSE(translateOf(0xB0, 7, 100).has_value());  // volume
    CHECK_FALSE(translateOf(0xB0, 66, 127).has_value()); // sostenuto
    CHECK_FALSE(translateOf(0xB0, 67, 127).has_value()); // soft pedal
    CHECK_FALSE(translateOf(0xB0, 123, 0).has_value());  // all notes off
}

TEST_CASE("message kinds the application does not model are ignored", "[midi][translation]")
{
    CHECK_FALSE(translateOf(0xA0, 60, 100).has_value()); // polyphonic aftertouch
    CHECK_FALSE(translateOf(0xC0, 5, 0).has_value());    // program change
    CHECK_FALSE(translateOf(0xD0, 90, 0).has_value());   // channel aftertouch
    CHECK_FALSE(translateOf(0xE0, 0, 64).has_value());   // pitch bend
    CHECK_FALSE(translateOf(0xF8, 0, 0).has_value());    // clock
}

// Input from a device is input from outside, so malformed input is malformed
// input rather than a programming error -- the same rule the MusicXML importer
// applies to files.
TEST_CASE("a malformed message yields nothing rather than reading past it", "[midi][translation]")
{
    SECTION("a null pointer")
    {
        CHECK_FALSE(translate(nullptr, 3, 0.0, 0.0).has_value());
    }

    SECTION("a zero length")
    {
        const auto bytes = message(0x90, 60, 100);

        CHECK_FALSE(translate(bytes.data(), 0, 0.0, 0.0).has_value());
    }

    SECTION("a negative length")
    {
        const auto bytes = message(0x90, 60, 100);

        CHECK_FALSE(translate(bytes.data(), -1, 0.0, 0.0).has_value());
    }

    SECTION("truncated to one and two bytes")
    {
        const auto bytes = message(0x90, 60, 100);

        CHECK_FALSE(translate(bytes.data(), 1, 0.0, 0.0).has_value());
        CHECK_FALSE(translate(bytes.data(), 2, 0.0, 0.0).has_value());
    }

    SECTION("a data byte with its top bit set")
    {
        // Not the message its status byte claims -- a running-status stream read
        // from the wrong offset, most likely. Guessing would invent notes.
        CHECK_FALSE(translateOf(0x90, 0x80, 100).has_value());
        CHECK_FALSE(translateOf(0x90, 60, 0x90).has_value());
    }
}

TEST_CASE("both timestamps are carried through unchanged", "[midi][translation]")
{
    const auto event = translateOf(0x90, 60, 100, 12.5, 980.25);

    REQUIRE(event.has_value());
    CHECK(event->deviceTimestampSeconds == Catch::Approx(12.5));
    CHECK(event->receivedTimestampSeconds == Catch::Approx(980.25));
}

TEST_CASE("a longer message is read from its first three bytes", "[midi][translation]")
{
    // A buffer carrying more than one message, or a message with trailing
    // bytes: the first three are the message, and reading more would be reading
    // the next one.
    const std::array<std::uint8_t, 6> bytes{0x90, 60, 100, 0x90, 62, 100};

    const auto event = translate(bytes.data(), static_cast<int>(bytes.size()), 0.0, 0.0);

    REQUIRE(event.has_value());
    CHECK(event->noteNumber == 60);
}
