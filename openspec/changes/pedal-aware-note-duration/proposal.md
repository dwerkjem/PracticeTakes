## Why

**How long a piano note sounds is not how long its key is held.** The sustain
pedal lifts every damper, so a released key keeps sounding until the pedal
comes up. A duration judgement that looks only at key down and key up marks
correct, pedalled playing as short — and that is worse than not judging
duration at all, because the player cannot tell the tool is the one in error.
#203 asks for the piano half of #44, which was written for a voice, where the
sung note and the sounding note are the same thing.

`midi-keyboard-capture` (#199) has delivered the raw material and, reading it
back, two things stand in the way:

- **The pedal's position is captured but not named.** CC64 is a continuous
  controller; a half-pedalling keyboard sends 0–127. `translate` already
  stores the value, in `MidiEvent::velocity`, whose comment says nothing about
  pedals. Nothing can be built on a field whose meaning is a coincidence.
- **The threshold is decided too early.** `translate` collapses the position to
  `pedalDown` against a constant 64. #203 requires the half-pedal threshold to
  be *"a visible setting, not a constant"*, and a verdict computed from a
  boolean that was fixed at capture time cannot honour a setting changed
  afterwards.

And one question was left for this change on purpose: `midi-keyboard-capture`
asks *"what happens to held notes when a device disconnects?"* and says #203
*"will have an opinion"*. It does — see design.

## What Changes

- **`MidiEvent` names the pedal position.** A `pedalPosition` field (0–127)
  for `sustainPedal` events; `velocity` goes back to meaning velocity only.
  `pedalDown` is **removed**: two readings of one controller, one fixed and one
  configurable, is a trap. Every reader of it moves to the tracker's threshold.
- **A JUCE-free `NoteSoundingTracker`** in `src/platform/midi/`. Fed the
  drained event stream in order, it emits one `SoundedNote` per note with key
  down, key up, when it stopped *sounding*, and what ended it — the finger, the
  pedal, a re-strike, or loss of the stream.
- **A JUCE-free duration judgement**, `judgeSoundingDuration`: a notated
  duration in seconds and a `SoundedNote` in, a verdict out — short, on target,
  or long — together with whether the pedal or the finger was holding the note.
  The notated duration is an input because producing it is #42's matching, which
  does not exist yet.
- **One persisted setting**, `midi.pedalDownThreshold`, default 64, schema 6 → 7.
- **A consumer.** The MIDI settings panel gains a threshold control and its live
  readout shows each note's finger duration, sounding duration and a *pedal*
  tag — the same lesson as #199: infrastructure with no consumer is
  infrastructure whose assumptions are untested.

Out of scope, and said so rather than left implicit:

- **Matching played notes to score notes** (#42), and so the notated duration
  the judgement needs. Until then the judgement is exercised by tests and not
  by the application.
- **Importing MusicXML pedal markings.** They are outside the imported subset
  (`docs/development/formats/musicxml-subset.md`). Pedal use is therefore
  *reported but never compared to the score*, and the report says so in words
  instead of evaluating against nothing and implying agreement.
- A model of string decay, una corda and sostenuto pedals, half-damping
  *effects* below the threshold, and note-length tolerances as a user setting
  (#44 and #50 own that).

## Capabilities

### New Capabilities

- **`pedal-aware-note-duration`** — each played note's sounding duration is
  derived from key and pedal state, distinguished from how long the key was
  held, and judged against a notated duration; the pedal threshold is a visible,
  persisted setting.

### Modified Capabilities

- **`midi-keyboard-capture`** — the sustain-pedal event states its position,
  not only a down/up reading. This capability is still an active change
  (task 7.5, hardware verification, is open), so the amendment is recorded here
  and folded in when that change archives.
- **`settings-transfer`** — one new persisted key. Additive.

## Impact

- **`src/platform/midi/`** — `MidiEvent.h`/`MidiMessageTranslation` change;
  two new headers (`NoteSoundingTracker.h`, `SoundingDurationJudgement.h`).
  All JUCE-free; no change to `MidiInputService` or its callback, and nothing
  new runs on the driver thread.
- **Settings** — one key, schema 6 → 7; an older file loads as `migrated`
  with the default.
- **Settings window** — the existing MIDI panel gains a control and richer
  readout. No new panel.
- **Verification gap inherited, not closed.** #199's hardware check (7.5) has
  not happened. This change's event path is therefore tested against byte
  arrays and synthetic streams only; a real keyboard's half-pedal behaviour is
  unobserved. Recorded as a task, not assumed.
- **Downstream** — #42/#44 supply the notated duration; #202 reuses the
  tracker's per-note output for chords.
