#pragma once

#include <cstdint>
#include <optional>

#include "MidiEvent.h"

// Raw MIDI bytes to the application's own event (no JUCE dependency).
//
// Taking bytes rather than a `juce::MidiMessage` is the decision that makes
// "the event stream is covered by tests that need no MIDI hardware" mean
// something: every rule below is asserted from a byte array, with no device and
// no JUCE object anywhere near the test.
//
// It is also what keeps the driver callback allocation-free. `juce::MidiMessage`
// owns a heap allocation for any payload over four bytes, so the callback reads
// `getRawData()`/`getRawDataSize()` and never copies the message.
//
// What is translated, and nothing else:
//
//  - note on and note off, with velocity and channel;
//  - the sustain pedal, CC64.
//
// Every other message -- aftertouch, program change, pitch bend, other
// controllers, sysex, clock -- yields nothing, and that is not an error. A
// keyboard sends plenty the application has no use for, and a capture path that
// treated the ordinary as exceptional would be wrong about every real device.
namespace midi
{
// MIDI's own numbering, for a reader checking this against a spec.
inline constexpr std::uint8_t noteOffStatus = 0x80;
inline constexpr std::uint8_t noteOnStatus = 0x90;
inline constexpr std::uint8_t controlChangeStatus = 0xB0;

// The sustain pedal's controller number.
inline constexpr std::uint8_t sustainPedalController = 64;

// At or above this, the pedal is down.
//
// Written as `== 127` surprisingly often, which is wrong for every keyboard
// that half-pedals: those send a continuous value, and a pedal held at 100 is
// a pedal that is down. 64 is the midpoint the MIDI specification names.
inline constexpr std::uint8_t sustainPedalDownThreshold = 64;

// Translate one message, or nothing.
//
// Returns nothing for a message this does not model, for a message shorter than
// its status byte requires, for a null pointer, and for a non-positive size.
// Malformed input from a device is malformed input rather than a programming
// error -- the same rule the MusicXML importer applies to files, for the same
// reason: it comes from outside and we do not get to assume it is well formed.
[[nodiscard]] std::optional<MidiEvent> translate(
    const std::uint8_t* data,
    int size,
    double deviceTimestampSeconds,
    double receivedTimestampSeconds) noexcept;
} // namespace midi
