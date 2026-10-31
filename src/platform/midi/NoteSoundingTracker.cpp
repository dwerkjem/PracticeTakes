#include "NoteSoundingTracker.h"

#include <algorithm>

namespace midi
{
namespace
{
[[nodiscard]] constexpr bool validChannel(int channel) noexcept
{
    return channel >= 1 && channel <= 16;
}

[[nodiscard]] constexpr bool validNote(int noteNumber) noexcept
{
    return noteNumber >= 0 && noteNumber <= 127;
}
} // namespace

void NoteSoundingTracker::setPedalThreshold(int newThreshold) noexcept
{
    threshold = std::clamp(newThreshold, minimumPedalDownThreshold, maximumPedalDownThreshold);
}

NoteSoundingTracker::Slot& NoteSoundingTracker::slotFor(int channel, int noteNumber) noexcept
{
    return slots[static_cast<std::size_t>((channel - 1) * noteCount + noteNumber)];
}

bool NoteSoundingTracker::pedalIsDown(const ChannelPedal& channelPedal) const noexcept
{
    return channelPedal.known && channelPedal.position >= threshold;
}

void NoteSoundingTracker::close(
    Slot& slot,
    int channel,
    int noteNumber,
    double soundingEndSeconds,
    double keyUpSeconds,
    SoundingEnd end,
    PedalCertainty certainty,
    std::vector<SoundedNote>& finished)
{
    SoundedNote note;
    note.channel = channel;
    note.noteNumber = noteNumber;
    note.velocity = slot.velocity;
    note.keyDownSeconds = slot.keyDownSeconds;
    note.keyUpSeconds = keyUpSeconds;
    note.soundingEndSeconds = soundingEndSeconds;
    note.end = end;
    note.pedal = certainty;
    finished.push_back(note);

    slot = Slot{};
    --openNotes;
}

void NoteSoundingTracker::consume(const MidiEvent& event, std::vector<SoundedNote>& finished)
{
    switch (event.kind)
    {
    case MidiEventKind::noteOn:
        noteOn(event, finished);
        break;
    case MidiEventKind::noteOff:
        noteOff(event, finished);
        break;
    case MidiEventKind::sustainPedal:
        pedal(event, finished);
        break;
    }
}

void NoteSoundingTracker::noteOn(const MidiEvent& event, std::vector<SoundedNote>& finished)
{
    if (!validChannel(event.channel) || !validNote(event.noteNumber))
    {
        return;
    }

    const double now = event.deviceTimestampSeconds;
    Slot& slot = slotFor(event.channel, event.noteNumber);

    // The hammer restarts the string, so a key struck again ends whatever was
    // still sounding from the last strike -- whether that strike's key is still
    // down (a stream with a missing note off) or only the pedal is holding it.
    if (slot.state == SlotState::keyDown)
    {
        close(
            slot, event.channel, event.noteNumber, now, now, SoundingEnd::restruck,
            PedalCertainty::known, finished);
    }
    else if (slot.state == SlotState::awaitingPedalRelease)
    {
        close(
            slot, event.channel, event.noteNumber, now, slot.keyUpSeconds, SoundingEnd::restruck,
            PedalCertainty::known, finished);
    }

    slot.state = SlotState::keyDown;
    slot.velocity = event.velocity;
    slot.keyDownSeconds = now;
    slot.keyUpSeconds = now;
    ++openNotes;
}

void NoteSoundingTracker::noteOff(const MidiEvent& event, std::vector<SoundedNote>& finished)
{
    if (!validChannel(event.channel) || !validNote(event.noteNumber))
    {
        return;
    }

    Slot& slot = slotFor(event.channel, event.noteNumber);

    // A note off for a key that is not down: nothing to end. Either it never had
    // a note on (a stream joined mid-phrase) or it was already released and is
    // waiting on the pedal, in which case this is a duplicate.
    if (slot.state != SlotState::keyDown)
    {
        return;
    }

    const double now = event.deviceTimestampSeconds;
    const ChannelPedal& channelPedal = pedals[static_cast<std::size_t>(event.channel - 1)];

    if (!channelPedal.known)
    {
        // Not knowing whether the pedal was holding this note is not the same as
        // the pedal being up. Say so rather than guess.
        close(
            slot, event.channel, event.noteNumber, now, now, SoundingEnd::finger,
            PedalCertainty::unknown, finished);
        return;
    }

    if (pedalIsDown(channelPedal))
    {
        slot.state = SlotState::awaitingPedalRelease;
        slot.keyUpSeconds = now;
        return;
    }

    close(
        slot, event.channel, event.noteNumber, now, now, SoundingEnd::finger, PedalCertainty::known,
        finished);
}

void NoteSoundingTracker::pedal(const MidiEvent& event, std::vector<SoundedNote>& finished)
{
    if (!validChannel(event.channel))
    {
        return;
    }

    ChannelPedal& channelPedal = pedals[static_cast<std::size_t>(event.channel - 1)];
    channelPedal.known = true;
    channelPedal.position = std::clamp(event.pedalPosition, 0, 127);

    if (pedalIsDown(channelPedal))
    {
        return;
    }

    // The pedal is up: every note on this channel that was waiting on it stops
    // sounding now. Notes whose keys are still down are unaffected -- the finger
    // is holding those.
    for (int noteNumber = 0; noteNumber < noteCount; ++noteNumber)
    {
        Slot& slot = slotFor(event.channel, noteNumber);

        if (slot.state == SlotState::awaitingPedalRelease)
        {
            close(
                slot, event.channel, noteNumber, event.deviceTimestampSeconds, slot.keyUpSeconds,
                SoundingEnd::pedal, PedalCertainty::known, finished);
        }
    }
}

void NoteSoundingTracker::invalidate(double atSeconds, std::vector<SoundedNote>& finished)
{
    for (int channel = 1; channel <= channelCount; ++channel)
    {
        for (int noteNumber = 0; noteNumber < noteCount; ++noteNumber)
        {
            Slot& slot = slotFor(channel, noteNumber);

            if (slot.state == SlotState::keyDown)
            {
                // Never seen to come up: it ends when the stream stopped being
                // trustworthy, and how long the finger held it is not known.
                close(
                    slot, channel, noteNumber, atSeconds, atSeconds, SoundingEnd::abandoned,
                    PedalCertainty::unknown, finished);
            }
            else if (slot.state == SlotState::awaitingPedalRelease)
            {
                close(
                    slot, channel, noteNumber, atSeconds, slot.keyUpSeconds, SoundingEnd::abandoned,
                    PedalCertainty::unknown, finished);
            }
        }
    }

    pedals = {};
}
} // namespace midi
