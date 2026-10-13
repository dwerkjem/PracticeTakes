#include <catch2/catch_test_macros.hpp>

#include "platform/midi/MidiInputService.h"
#include "support/MidiInputServiceCallbackAccess.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace
{
// MidiInputService is a juce::Timer, which asserts without a MessageManager --
// the same note ServiceUnderTest in AudioInputServiceTests.cpp carries.
struct ServiceUnderTest
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    std::unique_ptr<MidiInputService> owned = std::make_unique<MidiInputService>();
    MidiInputService& service = *owned;
};

// A consumer is an identity and nothing else: it registers, and then reads its
// own FIFO on its own schedule. Nothing is pushed to it.
struct TestConsumer final : MidiInputService::Consumer
{
};

[[nodiscard]] std::vector<midi::MidiEvent>
drain(MidiInputService& service, MidiInputService::Consumer& consumer)
{
    std::array<midi::MidiEvent, 2048> buffer{};
    const std::size_t count = service.readEvents(&consumer, buffer.data(), buffer.size());

    return {buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(count)};
}
} // namespace

TEST_CASE("a service with no device selected is not an error state", "[midi][service]")
{
    ServiceUnderTest fixture;

    CHECK(fixture.service.connectionState() == MidiInputService::ConnectionState::noDeviceSelected);
    CHECK(fixture.service.selectedDeviceIdentifier().isEmpty());
    CHECK(fixture.service.selectedDeviceName().isEmpty());
    CHECK(fixture.service.receivedEventCount() == 0);
}

// The "remembered keyboard is unplugged" case, which is the one a user meets
// every time they open the application before switching the piano on.
TEST_CASE("a selected device that is not present is remembered by name", "[midi][service]")
{
    ServiceUnderTest fixture;

    fixture.service.selectDevice("no-such-identifier", "Casio PX-S1100");

    CHECK(fixture.service.selectedDeviceIdentifier() == "no-such-identifier");
    CHECK(fixture.service.selectedDeviceName() == "Casio PX-S1100");
    CHECK(
        fixture.service.connectionState() ==
        MidiInputService::ConnectionState::selectedDeviceUnavailable);
}

TEST_CASE("selecting nothing returns to having no device", "[midi][service]")
{
    ServiceUnderTest fixture;
    fixture.service.selectDevice("no-such-identifier", "Casio PX-S1100");

    fixture.service.selectDevice({}, {});

    CHECK(fixture.service.selectedDeviceIdentifier().isEmpty());
    CHECK(fixture.service.connectionState() == MidiInputService::ConnectionState::noDeviceSelected);
}

TEST_CASE("enumerating devices does not require one to exist", "[midi][service]")
{
    ServiceUnderTest fixture;

    // No assertion on the contents: a build machine may have MIDI devices or
    // none, and both are correct. What matters is that asking is safe and that
    // every entry is usable if there is one.
    for (const MidiInputService::DeviceDescription& device : fixture.service.availableDevices())
    {
        CHECK(device.identifier.isNotEmpty());
    }

    SUCCEED("enumeration completed");
}

// Everything below drives the driver callback directly, through the friend-
// declared harness. That is what makes the fan-out testable with no MIDI
// hardware anywhere -- and it is also what makes the non-blocking annotation on
// that callback mean something, since RealtimeSanitizer only observes code that
// actually runs.

TEST_CASE("an event reaches a registered consumer", "[midi][service]")
{
    ServiceUnderTest fixture;
    TestConsumer consumer;
    fixture.service.addConsumer(&consumer);

    MidiInputServiceCallbackAccess::deliverNoteOn(fixture.service, 60, 100);

    const std::vector<midi::MidiEvent> events = drain(fixture.service, consumer);

    REQUIRE(events.size() == 1);
    CHECK(events.front().kind == midi::MidiEventKind::noteOn);
    CHECK(events.front().noteNumber == 60);
    CHECK(events.front().velocity == 100);
    CHECK(fixture.service.receivedEventCount() == 1);

    fixture.service.removeConsumer(&consumer);
}

TEST_CASE("events arrive in order", "[midi][service]")
{
    ServiceUnderTest fixture;
    TestConsumer consumer;
    fixture.service.addConsumer(&consumer);

    for (int note = 60; note < 65; ++note)
    {
        MidiInputServiceCallbackAccess::deliverNoteOn(fixture.service, note, 90);
    }

    const std::vector<midi::MidiEvent> events = drain(fixture.service, consumer);

    REQUIRE(events.size() == 5);

    for (std::size_t index = 0; index < events.size(); ++index)
    {
        CHECK(events[index].noteNumber == 60 + static_cast<int>(index));
    }

    fixture.service.removeConsumer(&consumer);
}

TEST_CASE("every consumer receives every event", "[midi][service]")
{
    ServiceUnderTest fixture;
    TestConsumer first;
    TestConsumer second;
    fixture.service.addConsumer(&first);
    fixture.service.addConsumer(&second);

    MidiInputServiceCallbackAccess::deliverNoteOn(fixture.service, 64, 70);

    CHECK(drain(fixture.service, first).size() == 1);
    CHECK(drain(fixture.service, second).size() == 1);

    fixture.service.removeConsumer(&first);
    fixture.service.removeConsumer(&second);
}

// The property the per-consumer FIFO exists for: one slow consumer must not be
// able to starve another. It is the same guarantee AudioInputService makes for
// a slow analysis tool.
TEST_CASE("a consumer that never drains does not starve another", "[midi][service]")
{
    ServiceUnderTest fixture;
    TestConsumer greedy;
    TestConsumer idle;
    fixture.service.addConsumer(&greedy);
    fixture.service.addConsumer(&idle);

    // Well past the FIFO's capacity, so the idle consumer is overflowing
    // throughout.
    for (int index = 0; index < 4000; ++index)
    {
        MidiInputServiceCallbackAccess::deliverNoteOn(fixture.service, 60, 100);

        if (index % 10 == 0)
        {
            static_cast<void>(drain(fixture.service, greedy));
        }
    }

    static_cast<void>(drain(fixture.service, greedy));

    MidiInputServiceCallbackAccess::deliverNoteOn(fixture.service, 72, 55);

    const std::vector<midi::MidiEvent> events = drain(fixture.service, greedy);

    REQUIRE_FALSE(events.empty());
    CHECK(events.back().noteNumber == 72);
    CHECK(fixture.service.lostEvents(&greedy) == 0);

    // And the idle one lost events rather than the service blocking or growing.
    CHECK(fixture.service.lostEvents(&idle) > 0);

    fixture.service.removeConsumer(&greedy);
    fixture.service.removeConsumer(&idle);
}

TEST_CASE("a removed consumer stops receiving events", "[midi][service]")
{
    ServiceUnderTest fixture;
    TestConsumer consumer;
    fixture.service.addConsumer(&consumer);
    fixture.service.removeConsumer(&consumer);

    MidiInputServiceCallbackAccess::deliverNoteOn(fixture.service, 60, 100);

    CHECK(drain(fixture.service, consumer).empty());
    CHECK(fixture.service.availableEvents(&consumer) == 0);
}

TEST_CASE("an unregistered consumer reads nothing rather than failing", "[midi][service]")
{
    ServiceUnderTest fixture;
    TestConsumer stranger;

    MidiInputServiceCallbackAccess::deliverNoteOn(fixture.service, 60, 100);

    CHECK(fixture.service.availableEvents(&stranger) == 0);
    CHECK(fixture.service.lostEvents(&stranger) == 0);
    CHECK(drain(fixture.service, stranger).empty());
}

TEST_CASE("registering the same consumer twice uses one slot", "[midi][service]")
{
    ServiceUnderTest fixture;
    TestConsumer consumer;
    fixture.service.addConsumer(&consumer);
    fixture.service.addConsumer(&consumer);

    MidiInputServiceCallbackAccess::deliverNoteOn(fixture.service, 60, 100);

    // One slot, so one copy of the event -- not two.
    CHECK(drain(fixture.service, consumer).size() == 1);

    fixture.service.removeConsumer(&consumer);
}

TEST_CASE("a message the application does not model reaches nobody", "[midi][service]")
{
    ServiceUnderTest fixture;
    TestConsumer consumer;
    fixture.service.addConsumer(&consumer);

    // Pitch bend, and a truncated note on.
    const std::array<std::uint8_t, 3> bend{0xE0, 0x00, 0x40};
    MidiInputServiceCallbackAccess::deliver(fixture.service, bend.data(), 3, 0.0);

    const std::array<std::uint8_t, 2> truncated{0x90, 60};
    MidiInputServiceCallbackAccess::deliver(fixture.service, truncated.data(), 2, 0.0);

    CHECK(drain(fixture.service, consumer).empty());

    // And nothing was counted for either: the count is of events delivered, not
    // of bytes seen.
    CHECK(fixture.service.receivedEventCount() == 0);

    fixture.service.removeConsumer(&consumer);
}

TEST_CASE("the sustain pedal reaches a consumer as a pedal event", "[midi][service]")
{
    ServiceUnderTest fixture;
    TestConsumer consumer;
    fixture.service.addConsumer(&consumer);

    const std::array<std::uint8_t, 3> down{0xB0, 64, 127};
    const std::array<std::uint8_t, 3> up{0xB0, 64, 0};
    MidiInputServiceCallbackAccess::deliver(fixture.service, down.data(), 3, 0.0);
    MidiInputServiceCallbackAccess::deliver(fixture.service, up.data(), 3, 0.0);

    const std::vector<midi::MidiEvent> events = drain(fixture.service, consumer);

    REQUIRE(events.size() == 2);
    CHECK(events[0].kind == midi::MidiEventKind::sustainPedal);
    CHECK(events[0].pedalDown);
    CHECK(events[1].kind == midi::MidiEventKind::sustainPedal);
    CHECK_FALSE(events[1].pedalDown);

    fixture.service.removeConsumer(&consumer);
}

TEST_CASE("pending events can be discarded by a consumer", "[midi][service]")
{
    ServiceUnderTest fixture;
    TestConsumer consumer;
    fixture.service.addConsumer(&consumer);

    MidiInputServiceCallbackAccess::deliverNoteOn(fixture.service, 60, 100);
    REQUIRE(fixture.service.availableEvents(&consumer) == 1);

    fixture.service.discardPendingEvents(&consumer);

    CHECK(fixture.service.availableEvents(&consumer) == 0);

    fixture.service.removeConsumer(&consumer);
}

TEST_CASE("a consumer registering later does not receive earlier events", "[midi][service]")
{
    ServiceUnderTest fixture;
    TestConsumer consumer;

    MidiInputServiceCallbackAccess::deliverNoteOn(fixture.service, 60, 100);

    fixture.service.addConsumer(&consumer);

    CHECK(drain(fixture.service, consumer).empty());

    fixture.service.removeConsumer(&consumer);
}
