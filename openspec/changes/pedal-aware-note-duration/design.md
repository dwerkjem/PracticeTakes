## Context

`midi-keyboard-capture` delivers `MidiEvent`s — note on, note off, sustain
pedal — to a consumer on the message thread, in order, with device timestamps
good "for the interval between two MIDI events and for nothing else". That is
exactly what this change needs: durations are intervals.

What a reader needs to know about the current code:

- `translate` already keeps CC64's raw value, in `MidiEvent::velocity`, and
  derives `pedalDown = value >= 64` from the constant
  `sustainPedalDownThreshold`.
- `MidiEventFifo` drops the **oldest** event on overflow and counts the loss.
  That was chosen because no event is safe to lose; it means a consumer that
  tracks *state* across events must treat a rise in the lost counter as "my
  state may now be wrong".
- Nothing in `src/features` reads MIDI. The settings panel's readout is the one
  consumer.

The physical model, stated so the code can be held to it. A piano damper lifts
while the pedal is down and falls when it comes up; a string keeps sounding
while *either* its key is held *or* the pedal is down. So a note's sounding
interval is

```
start = key down
end   = key up                       if the pedal is up at key up
      = the next pedal release       if the pedal is down at key up
```

with two corrections: a **re-strike** of the same key ends the previous sound
(the hammer restarts the string), and a note whose key came up while the pedal
was *up* is not rescued by a pedal pressed afterwards — the damper has already
fallen.

## Goals / Non-Goals

**Goals**

- A note released early but held by the pedal is not reported short.
- A note released with the pedal up is reported as sounding exactly as long as
  the key was held.
- Finger-held and pedal-held are distinguishable in the output.
- The half-pedal threshold is a persisted, user-visible setting that the
  judgement honours when changed *after* capture.
- Everything derivable from a byte-level or event-level stream is tested with no
  hardware and no JUCE.
- A damaged stream (lost events, disconnect) produces an honest "don't know",
  not a confident wrong verdict.

**Non-Goals**

- Matching to the score (#42), tolerance as a user setting (#44/#50), MusicXML
  pedal import, other pedals, decay modelling, multi-device capture.

## Decisions

### Carry the pedal's position; take the threshold away from capture

`MidiEvent` gains `int pedalPosition` (0–127), set only for `sustainPedal`.
`pedalDown` is removed, and `velocity` is no longer written for pedal events.

The alternative is to keep `pedalDown` as "the reading at the default
threshold" and add the position beside it. Rejected: it leaves two answers to
"is the pedal down?" in one struct, one of which silently ignores the user's
setting, and the next reader will pick the convenient one. The cost of removal
is the pedal cases in `MidiMessageTranslationTests` (63/64/65/127), which move
to the tracker where the threshold now lives, and the panel's readout.

`sustainPedalDownThreshold` stops being a translation constant and becomes
`defaultPedalDownThreshold` — the default of a setting, not a fact about MIDI.
The comparison stays `>= threshold`, so the default keeps the existing
behaviour of treating 64 as down.

### A tracker that holds state, fed by value, with no clock of its own

`NoteSoundingTracker::consume(const MidiEvent&)` appends zero or more finished
`SoundedNote`s to a caller-supplied container (the caller owns allocation; this
is message-thread code but a vector it does not own keeps the tracker
trivially testable and free of hidden growth). It reads **only**
`deviceTimestampSeconds`, so every duration is an interval on one device clock
and nothing is invented about audio-clock relationships (#38).

```
SoundedNote {
  channel, noteNumber, velocity
  keyDownSeconds, keyUpSeconds, soundingEndSeconds
  SoundingEnd end            // finger | pedal | restruck | abandoned
  PedalCertainty pedal       // known | unknown
}
fingerSeconds()   = keyUp - keyDown
soundingSeconds() = soundingEnd - keyDown
heldByPedal()     = soundingEnd > keyUp
```

State is one slot per (channel, note) — 16 × 128 — for the key, plus the pedal
position per channel, because CC64 is per channel and a keyboard that splits
channels would otherwise cross-contaminate. The tracker stores the **position**
and applies the threshold at the moment it needs a decision, reading the
threshold from a value it is handed (`setPedalThreshold`), so changing the
setting mid-session affects the next decision and not a boolean frozen earlier.

Re-evaluating *already finished* notes under a new threshold is not offered. A
`SoundedNote` carries what happened; if a caller wants to re-judge history it
keeps the events. Rejected alternative: store every pedal transition on the
note so the threshold can be replayed. It is the more flexible design and
nobody has asked for it; the readout shows live notes and a changed threshold
applying from the next note is unsurprising.

### Pedal state is "unknown" until the stream proves otherwise

Two things make the pedal's starting position unknowable:

- **At capture start.** CC64 is sent on change, not periodically. A pedal
  already held down when the device opens is invisible until it moves.
- **After a lost event.** `MidiEventFifo` drops the oldest event on overflow; a
  dropped pedal-up leaves the tracker believing the pedal is down forever, and
  every later note would be reported sustained.

Treating "unknown" as "up" is the optimistic default and wrong in the direction
that produces false *short* verdicts — the failure this change exists to avoid.
So the tracker has a third state. A note released while the pedal state is
unknown is emitted with `PedalCertainty::unknown`, its `soundingEnd` is its
`keyUp`, and the judgement returns *no verdict* for it rather than guessing. The
state becomes known at the first pedal event on that channel. At application
start this costs one un-judged note for a player who has not touched the
pedal; that is the right price.

### Lost events and disconnection end every open note as `abandoned`

This is the answer to the question `midi-keyboard-capture` left open, and the
honest place is the consumer that tracked the notes — which is the tracker, so
no consumer has to remember. Two entry points, both explicit:

- `invalidate(atSeconds)` — the consumer calls it when the lost-event counter
  has advanced (a note-off may be gone) or the connection state leaves *open*.
- It closes every held key as `abandoned`, sets every channel's pedal to
  unknown, and judgement declines abandoned notes.

`atSeconds` is a device timestamp the caller must supply from the last event it
saw; the tracker never reads a wall clock. A disconnect before any event has
nothing to close.

### Judgement is a pure function on numbers

```
judgeSoundingDuration(notatedSeconds, SoundedNote, tolerance) -> Verdict
Verdict { Outcome outcome; HeldBy heldBy; }
Outcome: short | onTarget | long | notJudged
```

- `short` when `soundingSeconds < notated * (1 - tolerance)`.
- `long` when `soundingSeconds > notated * (1 + tolerance)` **and the note was
  finger-held**. A pedal-held note outrunning its notation is ordinary pedalling,
  not an error, and calling it long would reproduce the false-negative problem in
  the other direction. A pedalled note that sounds long is `onTarget`; the
  report still says `heldBy == pedal`.
- `notJudged` for `abandoned`, `unknown` pedal certainty, or a non-positive
  notated duration.

`tolerance` is a parameter with a defaulted constant, not a setting: how
forgiving to be belongs to #44/#50 and their transparency requirement. The
constant is named and documented so it is findable when they arrive.

### Pedal markings are reported, not compared

MusicXML pedal marks are outside the imported subset. The score model has
nowhere to put them, so any comparison would be against an empty set and would
read as "you pedalled correctly". Pedal use is therefore *reported* —
`heldByPedal` per note — and the readout states that pedal markings in the score
are not read. This is the acceptance wording's escape hatch ("say so rather than
silently evaluating against nothing") applied literally.

### Where the threshold lives

`midi.pedalDownThreshold`, an int 1–127 (0 would mean the pedal is always
down, which is not a setting but a fault). Default 64. Persisted with the
existing keys; clamped on load so a hand-edited file cannot produce 0.
Surfaces in the MIDI settings panel as a labelled slider with its value shown
as a number, since the point is that a user can *see* where the line is.
Whether it belongs in an exported settings file is decided in the tasks, not by
the codec default — a threshold is a preference about a keyboard rather than an
identifier of a machine, so the lean is to include it.

## Risks / Trade-offs

- **Unverified against hardware.** Real keyboards send pedal values in a ramp,
  may chatter around the threshold, and some send CC64 only as 0/127. The
  tracker treats a crossing as an event, so chatter near the threshold produces
  rapid down/up — correct per the rule and ugly in the readout. No debounce is
  added without having seen it. Tracked as a hardware task.
- **No decay model.** A note struck softly and held under pedal for ten seconds
  is "sounding" for ten seconds although it died away much earlier. The
  judgement compares to notation, where long ringing is not an error, so the
  effect is bounded; it would matter to a future "let it ring" feature.
- **Late pedal.** Pressing the pedal *after* key-up does not extend the note.
  Real pianos sometimes do (the string is still audible), and a player who
  "catches" a note this way will be judged short. This is the right
  conservative default for a teaching tool and is documented as a known
  simplification.
- **Removing `pedalDown`** touches existing tests and the panel; a mechanical
  change, listed as a task so it is not discovered late.
- **Two un-judged notes at session start** if the pedal is used immediately:
  accepted above.

## Migration Plan

Schema 6 → 7, additive; a v6 file loads `migrated` with threshold 64. No saved
workspace is affected. No data migration, no rollback beyond the previous
version ignoring an unknown key, which it already does.

## Open Questions

- **Is `soundingEnd` for a pedal-held note the pedal release, or the pedal
  release capped by some maximum?** Uncapped here (no decay model). If #44 wants
  a cap it is one parameter.
- **Do device timestamps behave on every backend?** `MidiEvent` documents them as
  "a backend-chosen origin". If a backend reports zero for every message, every
  duration is zero. Falling back to `receivedTimestampSeconds` is possible but
  its jitter is on the order of the quantities measured. Needs the hardware
  check before a decision.
- **Should per-channel pedal state exist at all?** Most keyboards use one channel.
  Per-channel is the correct reading of the protocol and costs 16 ints; kept
  unless the hardware check shows a keyboard sending CC64 on a different channel
  than its notes, in which case it becomes a bug rather than a feature.
