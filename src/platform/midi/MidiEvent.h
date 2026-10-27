#pragma once

#include <cstdint>
#include <type_traits>

// What a MIDI keyboard did, as the application models it (no JUCE dependency).
//
// This is the whole vocabulary the capture path carries: note on, note off, and
// the sustain pedal. Everything else a keyboard sends is dropped at the
// translation step, deliberately -- see MidiMessageTranslation.h.
//
// The type is a trivially copyable POD with no owning members, and that is
// load-bearing rather than tidy. The MIDI driver callback writes one of these
// into a preallocated FIFO, and the callback may not allocate: a member that
// owned storage would make writing one an allocation. The static_assert below
// is there so that stops being true at compile time rather than quietly.
namespace midi
{
enum class MidiEventKind
{
    noteOn,
    noteOff,
    sustainPedal
};

struct MidiEvent
{
    MidiEventKind kind = MidiEventKind::noteOff;

    // 1-16, as MIDI numbers channels in every document a reader will consult --
    // not the 0-15 the wire uses. Converted once, here, so that no consumer has
    // to remember which convention it is holding.
    int channel = 1;

    // 0-127. Meaningless for `sustainPedal`.
    int noteNumber = 0;

    // 0-127. On a note off this is the release velocity, which most keyboards
    // send as zero and a few send meaningfully.
    int velocity = 0;

    // Only meaningful for `sustainPedal`.
    bool pedalDown = false;

    // What the device's own clock said, in seconds on a backend-chosen origin.
    // Good for the interval between two MIDI events and for nothing else.
    double deviceTimestampSeconds = 0.0;

    // A reading taken at the top of the driver callback, on the same clock the
    // rest of the application uses for wall time.
    //
    // **Neither of these establishes a relationship between MIDI timing and
    // audio timing.** The offset between the two has not been measured, and a
    // consumer must not compute one from these fields: see #38, which is where
    // the two clocks get reconciled against a calibration signal and real
    // hardware. A number invented here would be used by #43's onset timing as
    // though it had been measured.
    double receivedTimestampSeconds = 0.0;
};

static_assert(
    std::is_trivially_copyable_v<MidiEvent>,
    "MidiEvent is written by the MIDI driver callback, which may not allocate. A member that "
    "owns storage breaks that.");

[[nodiscard]] constexpr bool isNote(const MidiEvent& event) noexcept
{
    return event.kind == MidiEventKind::noteOn || event.kind == MidiEventKind::noteOff;
}
} // namespace midi
