#pragma once

#include "platform/midi/MidiInputService.h"

#include <cstdint>

// Drives MidiInputService's driver callback from a test.
//
// Granted by a friend declaration in MidiInputService.h, the same bargain
// AudioInputServiceCallbackAccess makes for the audio callback, and for the
// same reason rather than for convenience: the non-blocking annotation on that
// callback is only checked by RealtimeSanitizer against code that actually
// runs. Until something drives the function, a green real-time check means
// nothing at all -- which is stated in as many words in
// docs/development/performance/audio-thread-safety.md, and is a mistake worth
// not making twice.
//
// It also lets the fan-out be tested with no MIDI device anywhere: these bytes
// are what a keyboard would have sent.
struct MidiInputServiceCallbackAccess
{
    static void deliver(
        MidiInputService& service,
        const std::uint8_t* data,
        int size,
        double deviceTimestampSeconds)
    {
        service.deliverMessage(data, size, deviceTimestampSeconds);
    }

    // One note on, for a test that only needs an event to arrive.
    static void deliverNoteOn(MidiInputService& service, int noteNumber, int velocity)
    {
        const std::uint8_t message[]{
            0x90, static_cast<std::uint8_t>(noteNumber), static_cast<std::uint8_t>(velocity)};

        deliver(service, message, 3, 0.0);
    }
};
