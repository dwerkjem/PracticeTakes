#include "MidiMessageTranslation.h"

namespace midi
{
namespace
{
// The top nibble is the message kind, the bottom nibble the channel.
[[nodiscard]] constexpr std::uint8_t statusKind(std::uint8_t status) noexcept
{
    return static_cast<std::uint8_t>(status & 0xF0);
}

// The wire numbers channels 0-15; everything a reader will consult numbers them
// 1-16. Converted once, here.
[[nodiscard]] constexpr int channelOf(std::uint8_t status) noexcept
{
    return static_cast<int>(status & 0x0F) + 1;
}
} // namespace

std::optional<MidiEvent> translate(
    const std::uint8_t* data,
    int size,
    double deviceTimestampSeconds,
    double receivedTimestampSeconds) noexcept
{
    // Every message this models is three bytes. A shorter one is truncated, and
    // reading its second or third byte would be reading past the buffer.
    if (data == nullptr || size < 3)
    {
        return std::nullopt;
    }

    const std::uint8_t status = data[0];
    const std::uint8_t first = data[1];
    const std::uint8_t second = data[2];

    // A data byte has its top bit clear. A message whose "data" does not is not
    // the message its status byte claims -- a running-status stream read from
    // the wrong offset, most likely -- and guessing at it would invent notes.
    if ((first & 0x80) != 0 || (second & 0x80) != 0)
    {
        return std::nullopt;
    }

    MidiEvent event;
    event.channel = channelOf(status);
    event.deviceTimestampSeconds = deviceTimestampSeconds;
    event.receivedTimestampSeconds = receivedTimestampSeconds;

    switch (statusKind(status))
    {
    case noteOnStatus:
        event.noteNumber = static_cast<int>(first);
        event.velocity = static_cast<int>(second);

        // A note on with velocity zero is a note off.
        //
        // Not a quirk worth being clever about: it is how a large fraction of
        // real keyboards release a key, because running status lets them send a
        // whole phrase under one note-on status byte. Getting this wrong is the
        // single most common cause of a note stuck on forever in a MIDI
        // consumer, and it is a translation rule -- so it is applied once, here,
        // rather than remembered by everything downstream.
        event.kind = second == 0 ? MidiEventKind::noteOff : MidiEventKind::noteOn;

        return event;

    case noteOffStatus:
        event.kind = MidiEventKind::noteOff;
        event.noteNumber = static_cast<int>(first);

        // Release velocity. Most keyboards send zero; a few mean it.
        event.velocity = static_cast<int>(second);

        return event;

    case controlChangeStatus:
        if (first != sustainPedalController)
        {
            // Every other controller is dropped, and dropping is not an error.
            return std::nullopt;
        }

        event.kind = MidiEventKind::sustainPedal;
        event.velocity = static_cast<int>(second);
        event.pedalDown = second >= sustainPedalDownThreshold;

        return event;

    default:
        return std::nullopt;
    }
}
} // namespace midi
