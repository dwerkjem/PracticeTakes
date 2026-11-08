## Context

The proposal (written before the practice clock, takes and their review)
stands. The maintainer approved designing and building it on 2026-11-08.
This settles what the proposal left open, choosing a default for each and
saying why. #296 then adds every way of choosing the target, and #297 the note
view; neither is built here.

## Decisions

### 1. The reference pitch is a value the application owns

`pitch::ReferencePitch` (`src/platform/pitch/`, JUCE-free) holds A4 in hertz:
- the default is 440;
- it is clamped to 415–466, from Baroque A415 to a semitone above 440;
- 1 Hz steps.

The conversions `frequencyToMidi` and `midiToFrequency` take it as a parameter,
so nothing can convert without one. `PitchTracker` reads it from its per-frame
`Settings`, like its other parameters, so the tracker still holds no
configuration of its own.

`MainComponent` owns it, and tools read it through
`ToolServices::referencePitch` on their own timers.
- **Where it is set:** Settings → Practice, beside the count-in. It is a
  musical preference about this practice room.
- **Where it is saved:** locally, like the metronome. A settings transfer
  carries tool settings, not practice settings.

A file from before it reads as A440.

### 2. The target is application state, chosen from the score for now

`pitch::CurrentTarget` holds:
- the target: its sounding MIDI number, its written and sounding names, and
  where it is in the score;
- or, when there is none, the reason in a sentence.

It is held like `CurrentScore`: a `shared_ptr<const ...>`, message thread only,
and a change is detected by pointer.

Until #296, the shell sets it whenever a score is published, by
`selectTargetNote`. #296 adds the other ways to set it, writing to this same
holder.

### 3. Only a part that plays one note at a time has a target

`selectTargetNote` now looks for the first **monophonic** part:
- no event holds more than one note;
- no two of its voices sound at once.

It takes that part's first pitched, non-grace note. A score whose parts are all
polyphonic gets no target, with the reason *"Every part plays more than one
note at a time; a target is one note"*.

The old chord fields (`isChordMember`, `chordNoteCount`) go, since a target can
no longer be in a chord. The sample song's target is the voice's first note,
not the piano's.

### 4. The tuner shows the target

A line under the tuner's reading says one of three things.

| Case | Text |
|---|---|
| Within the tuner's in-tune band (±5 ¢) | *"Target B3 · on it"* |
| Within half a semitone | *"Target B3 · 12 ¢ sharp"* |
| Further | *"Target B3 · 2 semitones below"* |

With no signal, only the target is shown. With no target, the line gives the
reason. `describeAgainstTarget` is JUCE-free and tested.

### 5. The Target Note tool leaves the catalog, and its ids stay

The ids `target-note`, `score-target` and `targetNote` become aliases of the
tuner, permanently. A workspace that names any of them opens a tuner, which is
where the target is shown now.

The tool's component, its factory and its catalog entry go. Its states are
removed, and `all-tools-docked` loses it. The pinned tool count drops by one.

`TargetNoteSelection` moves to `src/platform/score/`, because the shell, not a
tool, uses it now. Its tests move with it.

`adding-a-tool.md` gains "Removing a tool".
