# How long a played note sounds

`src/platform/midi/NoteSoundingTracker.h` turns the ordered MIDI event stream
into one `SoundedNote` per note: when the key went down, when it came up, and
when the note *stopped sounding*. `SoundingDurationJudgement.h` compares that
with a notated duration. Both are JUCE-free and run on the message thread; nothing
in this path runs on the MIDI driver callback.

## Why it is not just key down to key up

On a piano the sustain pedal lifts every damper, so a released key keeps
sounding until the pedal comes up. A duration judgement that looked only at the
key would mark correct, pedalled playing short — and a tool that is wrong in a
way the player cannot see is worse than a tool that does not judge duration.

A note sounds until:

| Pedal at key-up | The note ends at |
|---|---|
| up | the key coming up (`SoundingEnd::finger`) |
| down | the next time the pedal comes up (`SoundingEnd::pedal`) |
| *not known* | the key coming up, flagged `PedalCertainty::unknown` and **not judged** |

and in every case no later than the same key being struck again
(`SoundingEnd::restruck`), because the hammer restarts the string.

## Three states for the pedal, not two

The pedal's position is **unknown** until the first CC64 on its channel arrives,
and again after `invalidate`. CC64 is sent on change rather than periodically,
so a pedal already held when the device opens is invisible until it moves; and
`MidiEventFifo` drops the *oldest* event on overflow, so a lost pedal-up would
otherwise leave the tracker believing the pedal down for ever.

"Unknown means up" is the optimistic default, and it is wrong in the direction
that produces false *short* verdicts — the failure all of this exists to
prevent. A note released while the pedal is unknown is reported as such.

The price is every note released before the first pedal event on its channel. For
a player who uses the pedal that is the first few notes. For one who never moves
it, or whose keyboard has no pedal, it is *every* note, and "move the pedal once"
is not available to them. There is deliberately no "assume up" mode: assuming is
the guess this design refuses to make. An explicit, user-chosen "I have no pedal"
setting is the honest fix and belongs with whatever consumes the verdicts
(#42/#44).

## Lost events and disconnection

The previous design (`midi-keyboard-capture`) guessed this would be settled by
the *service* synthesising note-offs for everything it believed was held when a
device went away. It is settled by the tracker instead, deliberately:

- The service does not know a consumer tracks state, and a consumer that does is
  the only party that can say what a lost note-off *meant* for its picture.
- A synthesised note-off is indistinguishable from a real release. It would turn
  "the keyboard left" into a confident, judged duration. `abandoned` is a
  different outcome, and the judgement declines it.

So a consumer calls `invalidate(atSeconds)` when `lostEvents(consumer)` has
advanced or the connection leaves *open*, passing the last device timestamp it
saw. The tracker cannot see either condition, so the *detection* stays with the
consumer — the settings panel does it, and any later consumer must too; what the
tracker removes is the need to decide what a loss means for a held note. When the
selected device changes, the consumer must also discard the events already queued
for it before invalidating, or the old device's notes arrive after the reset. Every held note ends `abandoned`, and every channel's pedal becomes
unknown. The settings panel does this before it reads each batch, because
whatever was lost happened before the batch.

## The threshold

`MidiEvent` carries `pedalPosition` (0–127) exactly as sent. Where the pedal
counts as *down* is the setting `midi.pedalDownThreshold` (1–127, default 64),
applied by the tracker when it needs a decision. There is deliberately no
"pedal down" boolean on the event: a second answer to the same question that
ignores the setting is a trap.

A changed threshold affects the next decision. Finished notes are not
re-judged; a `SoundedNote` records what happened, and a caller that wants
history under another threshold keeps the events.

The threshold is **not** carried in a settings transfer file, and **Reset all**
does not clear it. It is calibration for a particular keyboard's pedal, the
same class of setting as the device selection, which also does not travel.

## Judgement

`judgeSoundingDuration(notatedSeconds, note, tolerance)`:

- **short** when the *sounding* duration is under the notated one by more than
  the tolerance;
- **long** only when it is over by more than the tolerance *and* a finger held
  it — a note ringing past its notation because the pedal was down is ordinary
  pedalling;
- **notJudged** for abandoned notes, unknown pedal state, or a non-positive
  notated duration;
- always reports whether the finger or the pedal was holding the note.

The tolerance is the named constant `defaultDurationTolerance`, not a setting.
How forgiving to be is #44's and #50's, with their transparency requirement.
The notated duration is an input because producing it is #42's matching, which
does not exist yet, so until then the judgement is exercised by tests and not by
the application.

## Known simplifications

- **Late pedal.** A pedal pressed only *after* the key came up does not extend
  the note. A real string may still be audible; here the damper has already
  fallen. A player who catches a note this way will be judged short.
- **No decay.** A soft note held by the pedal for ten seconds "sounds" for ten
  seconds.
- **Pedal markings are not compared.** MusicXML pedal marks are outside the
  imported subset (`../formats/musicxml-subset.md`), so pedal use is reported
  and never compared with the score, and the settings panel says so.
- **Device timestamps.** Durations are intervals on the device clock. Nothing
  here relates MIDI time to audio time (#38), and no real keyboard has yet been
  observed to confirm the timestamps advance.
