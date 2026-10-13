#pragma once

// Modular JUCE includes rather than the generated JuceHeader.h umbrella, for
// the reason AudioInputService.h records: JuceHeader.h is produced for the
// PracticeTakes application target only, so a file that uses it cannot be
// compiled into PracticeTakesTests. That is the mechanical reason a service
// ends up untested, and it is avoidable by not doing it.
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

// Shared with src/platform/audio, which is where it was written and still
// lives. The annotation is no longer about the audio thread alone -- this is
// the second driver callback in the application to carry it -- and that it sits
// under audio/ rather than somewhere neutral is noted in the design's open
// questions rather than fixed here, since moving it would touch
// AudioInputService and this change deliberately does not.
#include "../audio/RealtimeSafety.h"

#include "MidiEvent.h"
#include "MidiEventFifo.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Owns the application's MIDI input device and fans its events into one bounded
// SPSC FIFO per consumer. Consumers pull from their own FIFO on the message
// thread; no consumer code runs in the driver callback.
//
// A peer of AudioInputService rather than a passenger on it: this opens
// `juce::MidiInput` directly and does **not** go through the
// `juce::AudioDeviceManager` that AudioInputService owns. The reasons are in
// openspec/changes/midi-keyboard-capture/design.md, and the one that decides it
// is that routing MIDI through that manager would couple a keyboard's liveness
// to audio-device recovery -- the one subsystem here documented as able to block
// without limit. A pianist with a keyboard and no microphone is an ordinary
// user of this application.
class MidiInputService final : private juce::MidiInputCallback, private juce::Timer
{
  public:
    enum class ConnectionState
    {
        // No device has been chosen. Not an error: it is what a fresh install
        // and a user with no keyboard both look like.
        noDeviceSelected,

        // One has been chosen and is not currently available -- unplugged, or
        // not plugged in yet. The name is still known, which is what lets the
        // interface say which keyboard it is waiting for.
        selectedDeviceUnavailable,

        open
    };

    // A device as the interface offers it.
    struct DeviceDescription
    {
        juce::String identifier;
        juce::String name;
    };

    // What a consumer registers as. Nothing is pushed to it: it reads its own
    // FIFO on its own timer, the arrangement SharedPitchAnalysis chose for
    // audio, because nothing downstream needs to react faster than its own
    // refresh rate already samples.
    class Consumer
    {
      public:
        virtual ~Consumer() = default;
    };

    MidiInputService();
    ~MidiInputService() override;

    void addConsumer(Consumer* consumer);
    void removeConsumer(Consumer* consumer);

    [[nodiscard]] std::size_t availableEvents(Consumer* consumer) const;
    [[nodiscard]] std::size_t
    readEvents(Consumer* consumer, midi::MidiEvent* destination, std::size_t maximumEvents);
    void discardPendingEvents(Consumer* consumer);

    // Events dropped for this consumer because it did not drain in time. See
    // midi::MidiEventFifo: a value that has moved since the consumer last
    // looked means its idea of which notes are held is wrong, not that one
    // event went missing.
    [[nodiscard]] std::uint64_t lostEvents(Consumer* consumer) const;

    // Every MIDI input the system currently reports.
    [[nodiscard]] std::vector<DeviceDescription> availableDevices() const;

    // Choose one, by the identifier `availableDevices` gave. An empty
    // identifier selects nothing and closes whatever is open.
    void selectDevice(const juce::String& identifier, const juce::String& name);

    [[nodiscard]] juce::String selectedDeviceIdentifier() const;

    // The chosen device's name, remembered even while it is unavailable, so the
    // interface can say "Casio PX-S1100 (not connected)" rather than showing an
    // opaque identifier or a blank.
    [[nodiscard]] juce::String selectedDeviceName() const;

    [[nodiscard]] ConnectionState connectionState() const;

    // Total events seen since start, for a proof-of-life readout.
    [[nodiscard]] std::uint64_t receivedEventCount() const noexcept;

  private:
    // The driver callback is private and the MidiInputCallback base is
    // inherited privately, because nothing in the application may invoke it --
    // only the device does. A test must, though, for the reason
    // AudioInputService records: an annotation on a function nothing drives
    // proves nothing, and RealtimeSanitizer observes code that executes.
    //
    // One named type gets in, rather than widening the class's surface.
    friend struct MidiInputServiceCallbackAccess;

    static constexpr std::size_t maximumConsumers = 8;

    // A ten-finger chord with the pedal is about two dozen events and a
    // consumer drains many times a second, so this is roughly two orders of
    // magnitude of headroom. Dropping at all means a consumer stalled for
    // seconds.
    static constexpr std::size_t consumerFifoCapacity = 1024;

    static constexpr int serviceRefreshRateHz = 4;
    static constexpr int disconnectedDeviceScanIntervalTicks = serviceRefreshRateHz * 2;
    static constexpr int connectedDeviceScanIntervalTicks = serviceRefreshRateHz * 10;

    struct ConsumerSlot
    {
        midi::MidiEventFifo<consumerFifoCapacity> fifo;
        Consumer* consumer = nullptr;
        std::atomic<bool> active{false};
    };

    // noexcept and non-blocking are load-bearing, not decoration: see
    // RealtimeSafety.h. This is not the audio thread -- JUCE calls it on a
    // high-priority thread it owns per input device -- but it is a driver
    // callback and the same discipline applies, so it allocates nothing, takes
    // no lock, blocks on nothing, logs nothing, and touches no UI.
    void handleIncomingMidiMessage(
        juce::MidiInput* source,
        const juce::MidiMessage& message) noexcept PRACTICE_TAKES_NONBLOCKING override;

    // Translate and fan out. Annotated because the callback is its only caller.
    void deliverMessage(const std::uint8_t* data, int size, double deviceTimestampSeconds) noexcept
        PRACTICE_TAKES_NONBLOCKING;

    void timerCallback() override;

    // Open or close to match the selection and what is present. Message thread
    // only.
    void reconcileDevice();

    // The one call that touches the backend to open a device, behind a seam of
    // its own.
    //
    // AudioInputService runs its equivalent on a thread that may outlive the
    // service, because an ALSA reopen can block forever waiting on a PCM device
    // another process holds. A MIDI input opens a sequencer client rather than
    // preparing a PCM stream, and ALSA permits several readers of one, so that
    // failure mode has no equivalent here. That is a reasoned claim rather than
    // a measurement -- which is exactly why it is one call behind one seam: if
    // an open is ever seen to block, AudioInputService::startDeviceWork applies
    // here unchanged.
    [[nodiscard]] std::unique_ptr<juce::MidiInput> openDevice(const juce::String& identifier);

    [[nodiscard]] ConsumerSlot* findConsumer(Consumer* consumer);
    [[nodiscard]] const ConsumerSlot* findConsumer(Consumer* consumer) const;

    // Whether an identifier appears in the current enumeration. Static because
    // it asks the system rather than this object, and because reconcileDevice
    // needs the answer for two different identifiers.
    [[nodiscard]] static bool isDevicePresent(const juce::String& identifier);

    mutable juce::CriticalSection consumerLock;
    std::array<ConsumerSlot, maximumConsumers> consumers;

    // Guards the selection and the open device, both of which the message
    // thread reads and writes and the interface asks about. Never taken in the
    // driver callback.
    mutable juce::CriticalSection deviceLock;
    std::unique_ptr<juce::MidiInput> openInput;
    juce::String openIdentifier;
    juce::String chosenIdentifier;
    juce::String chosenName;

    std::atomic<std::uint64_t> eventsReceived{0};
    int ticksUntilNextScan = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiInputService)
};
