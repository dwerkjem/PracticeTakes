#pragma once

#include <array>
#include <vector>

#include "MidiEvent.h"
#include "MidiMessageTranslation.h"

// How long each played note actually sounded (no JUCE dependency).
//
// On a piano, how long a note sounds is not how long its key is held. The
// sustain pedal lifts every damper, so a released key keeps sounding until the
// pedal comes up. This turns the ordered event stream into one `SoundedNote`
// per note that says both things, so a duration judgement can use the one that
// matters and a report can tell the player which one held the note.
//
// The model, stated so the code can be held to it. A string sounds while its key
// is held *or* the pedal is down, so a note sounds until
//
//   - its key comes up, if the pedal is up at that moment;
//   - otherwise the next time the pedal comes up;
//   - and in either case no later than the same key being struck again, because
//     the hammer restarts the string.
//
// Two deliberate simplifications, both documented because both are wrong about
// some real playing:
//
//   - **Late pedal.** A pedal pressed only *after* the key came up does not
//     extend the note. On a real piano the string may still be audible and the
//     damper not yet fully down; here the damper has already fallen. The
//     conservative reading for a teaching tool, but a player who catches a note
//     this way will be judged short.
//   - **No decay.** A soft note held by the pedal for ten seconds "sounds" for
//     ten seconds although it died away long before. The consumer compares to
//     notation, where long ringing is not an error, so this is bounded.
//
// **Time base.** Only `deviceTimestampSeconds` is read, so every duration is an
// interval on one device clock. Nothing here relates MIDI time to audio time;
// that is #38.
//
// **Where the pedal threshold lives.** Here, and nowhere upstream. The event
// carries the pedal's *position*; this applies `setPedalThreshold` at the moment
// it needs a decision. A change of threshold therefore takes effect from the
// next decision. Already-finished notes are not re-judged: a `SoundedNote`
// records what happened, and a caller that wants history under another
// threshold keeps the events.
//
// **Pedal state is three-valued.** The pedal's position is unknown until the
// first pedal event on its channel, and again after `invalidate`. CC64 is sent
// on change, not periodically, so a pedal already held when the device opens is
// invisible until it moves; and `MidiEventFifo` drops the oldest event on
// overflow, so a lost pedal-up would otherwise leave this believing the pedal
// down forever. Treating "unknown" as "up" would be the optimistic default and
// wrong in the direction that produces false *short* verdicts -- the failure
// this exists to prevent. A note released while the pedal is unknown is emitted
// with `PedalCertainty::unknown` and is not judged.
//
// Message thread only. Nothing here runs on the MIDI driver callback.
namespace midi
{
enum class SoundingEnd
{
    // The key came up with the pedal up.
    finger,
    // The key came up with the pedal down, and the pedal then came up.
    pedal,
    // The same key was struck again.
    restruck,
    // Events were lost, or the device went away, before the note ended. How long
    // it sounded is not known.
    abandoned
};

enum class PedalCertainty
{
    known,
    // The pedal's position was not known when the key came up, so whether the
    // pedal was holding the note is not known either.
    unknown
};

struct SoundedNote
{
    int channel = 1;
    int noteNumber = 0;
    int velocity = 0;

    double keyDownSeconds = 0.0;
    // When the key came up. For a note that was re-struck while its key was still
    // held, or abandoned while its key was still held, the moment it ended.
    double keyUpSeconds = 0.0;
    double soundingEndSeconds = 0.0;

    SoundingEnd end = SoundingEnd::finger;
    PedalCertainty pedal = PedalCertainty::known;

    // How long the finger held the key.
    [[nodiscard]] constexpr double fingerSeconds() const noexcept
    {
        return keyUpSeconds - keyDownSeconds;
    }

    // How long the note sounded.
    [[nodiscard]] constexpr double soundingSeconds() const noexcept
    {
        return soundingEndSeconds - keyDownSeconds;
    }

    // Sounded past the key coming up, so something other than the finger held it.
    // Plain arithmetic: for an `abandoned` or `unknown`-pedal note it says nothing
    // reliable, and a judgement declines those before it reads this.
    [[nodiscard]] constexpr bool heldByPedal() const noexcept
    {
        return soundingEndSeconds > keyUpSeconds;
    }
};

class NoteSoundingTracker
{
  public:
    NoteSoundingTracker() = default;

    // Where the pedal counts as down: at or above this. Clamped to the range the
    // `midi.pedalDownThreshold` setting accepts. Applies from the next decision.
    void setPedalThreshold(int threshold) noexcept;

    [[nodiscard]] int pedalThreshold() const noexcept
    {
        return threshold;
    }

    // Feed one event, in the order it arrived. Notes that ended because of it are
    // appended to `finished`; the caller owns that container, so the tracker holds
    // no growing storage of its own.
    //
    // A stream that makes no sense is input rather than error: a note off with no
    // note on is ignored, a second note on for a held key re-strikes it, and a
    // pedal up with no prior pedal down only makes the pedal's state known.
    void consume(const MidiEvent& event, std::vector<SoundedNote>& finished);

    // The stream can no longer be trusted: events were lost (a note off may be
    // among them), or the device disconnected. Every held note, and every note
    // waiting on the pedal, ends as `abandoned` at `atSeconds`, and every
    // channel's pedal becomes unknown. Idempotent.
    //
    // `atSeconds` is a device timestamp the caller supplies -- the last event it
    // saw. The tracker never reads a wall clock.
    void invalidate(double atSeconds, std::vector<SoundedNote>& finished);

    // Notes whose key is down, or that are waiting on the pedal.
    [[nodiscard]] int openNoteCount() const noexcept
    {
        return openNotes;
    }

  private:
    enum class SlotState
    {
        idle,
        keyDown,
        awaitingPedalRelease
    };

    struct Slot
    {
        SlotState state = SlotState::idle;
        int velocity = 0;
        double keyDownSeconds = 0.0;
        double keyUpSeconds = 0.0;
    };

    struct ChannelPedal
    {
        bool known = false;
        int position = 0;
    };

    static constexpr int channelCount = 16;
    static constexpr int noteCount = 128;

    [[nodiscard]] Slot& slotFor(int channel, int noteNumber) noexcept;
    [[nodiscard]] bool pedalIsDown(const ChannelPedal& pedal) const noexcept;
    void close(
        Slot& slot,
        int channel,
        int noteNumber,
        double soundingEndSeconds,
        double keyUpSeconds,
        SoundingEnd end,
        PedalCertainty certainty,
        std::vector<SoundedNote>& finished);

    void noteOn(const MidiEvent& event, std::vector<SoundedNote>& finished);
    void noteOff(const MidiEvent& event, std::vector<SoundedNote>& finished);
    void pedal(const MidiEvent& event, std::vector<SoundedNote>& finished);

    std::array<Slot, channelCount * noteCount> slots{};
    std::array<ChannelPedal, channelCount> pedals{};
    int threshold = defaultPedalDownThreshold;
    int openNotes = 0;
};
} // namespace midi
