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
state becomes known at the first pedal event on that channel.

**What this costs, stated plainly:** every note released before the first pedal
event on its channel is un-judged. For a player who uses the pedal, that is the
notes before its first movement. For a player who never moves it — or whose
keyboard has no pedal at all — it is *every* note, and the panel's remedy ("move
the pedal once") is not available to them. The tracker deliberately offers no
"assume up" mode, because assuming is exactly the guess this design refuses to
make. Whether to give such a player an explicit, user-chosen "I have no pedal"
setting is a decision for whoever consumes the verdicts (#42/#44) — see Open
Questions.

### Lost events and disconnection end every open note as `abandoned`

This is the answer to the question `midi-keyboard-capture` left open. The tracker
is where the consequence of a loss is decided — what a lost note-off *means* for
a held note — but it cannot observe the loss itself: it never sees
`lostEvents(consumer)` or `connectionState()`. So the *detection* is still the
consumer's job, and `midi-keyboard-capture`'s worry that every consumer has to
remember is relocated and made cheap, not removed. The settings panel is the one
consumer that exists and does the wiring; #42 or #202 must do it too. There is one
entry point:

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
Outcome: tooShort | onTarget | tooLong | notJudged   (`short`/`long` are keywords)
```

- `tooShort` when `soundingSeconds < notated * (1 - tolerance)`.
- `tooLong` when `soundingSeconds > notated * (1 + tolerance)` **and the note was
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
It is **not** carried in a settings transfer file, and `applyTransferredSettings`
does not touch it — decided when implementing, reversing this section's earlier
lean. A threshold is calibration for one particular keyboard's pedal, the same
class of setting as the device selection, which `midi-keyboard-capture` already
declined to export for the same reason. Carrying it would also have meant a
transfer-format bump (2 → 3) for a value that does not travel meaningfully.

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
- **Un-judged notes until the pedal is first seen**: every note, for a player
  who never moves the pedal. Accepted above, with the cost stated there; the
  remedy for a pedal-less player is an open question, not a silent default.

## Migration Plan

**No schema bump.** The key is purely additive: a file without it loads with
threshold 64, and `AppSettings::load` clamps or defaults whatever it finds.

A bump is not merely unnecessary here, it is harmful. `AppSettings::load` treats
*any* stored schema other than the current one as the pre-workspace layout format
and replaces the saved workspace catalog and window bounds with defaults — so
bumping 6 → 7 would have wiped every existing user's saved workspaces, and the
first version of this change did exactly that (found by review; pinned now by a
test that stamps schema 6 explicitly). The same path was taken by
`midi-keyboard-capture`'s 5 → 6 bump, so that hazard already exists in `main`
and is not fixed by this change.

Rollback is clean for the same reason: an older build reads the file at its own
schema and ignores the unknown key. (A *bump* would have made the older build
see a newer schema and discard the whole file.)

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

## Answers, recorded after implementation

- **Transfer file.** Not carried; see *Where the threshold lives*. **Reset all**
  does not clear it either, matching the device selection.
- **Held notes at disconnect.** Settled in the tracker with `invalidate`, and not
  by the service synthesising note-offs as `midi-keyboard-capture` guessed; the
  reason is in `docs/development/architecture/note-sounding.md`.
- **Do device timestamps behave on every backend?** Still open. Only byte arrays
  and synthetic streams have been exercised; no keyboard has been played. The
  readout would show durations of 0.00 s if a backend reports a constant
  timestamp, which is the symptom to look for.
- **Should a player with no pedal be able to say so?** Today a pedal never seen
  leaves every note un-judged. An explicit user setting ("I have no pedal; treat
  it as up") would fix that without the tracker guessing, but where it lives and
  what it is called belongs with the consumer of the verdicts (#42/#44), not here.
- **Is per-channel pedal state right?** Kept, on the same evidence: untested
  against a real keyboard that puts CC64 on a different channel than its notes.
- **Cap on a pedal-held note.** None. No decay model, so no cap; one parameter if
  #44 wants it.
