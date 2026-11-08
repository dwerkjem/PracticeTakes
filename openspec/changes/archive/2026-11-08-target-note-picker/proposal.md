## Why

#227 made the target note application state, but the only way to set it is to
open a score: the target is then the score's first note. A singer warming up,
or a player working on one awkward note, needs to say "this note" directly.
#296 is that. The maintainer approved designing and building it on
2026-11-08.

## What Changes

- **A target picker**, opened by clicking the tuner's target line or from its
  options menu. The ways to choose:
  - **on a keyboard**: click a key;
  - **on a staff**: click a line or space, with ♭, ♮ or ♯ beforehand;
  - **by name**: type `F#4` or `Bb3`;
  - **by singing or playing it**: *Use the note I'm on*, enabled only while the
    tuner's reading is steady and close to a note;
  - **stepping**: up or down a semitone, or a step of the score's key.
- **From the score**: right-clicking a note in the Score View offers *Aim at
  G4*. A chord offers each of its notes; choosing one is the player's own
  choice, so the "one note at a time" rule does not apply.
- **The chosen target is kept.** It is remembered across restarts. A setting
  in Settings → Practice says whether opening a score replaces it with the
  score's first note (the default) or keeps it.
- **Written and sounding stay distinct.** A target chosen from a transposing
  part's note keeps both names.

## Capabilities

### New Capabilities
- `target-note-picker`: the ways to choose the target, keeping it, and what
  opening a score does to it.

### Modified Capabilities
- `target-note-state`: the target can be set by the player as well as by the
  score, and says which.

## Impact

- `pitch::CurrentTarget` is written by tools (the tuner's picker, the Score
  View), so `ToolServices::target` stops being `const`.
- New JUCE-free pieces: note names parsed, staff positions turned into
  pitches, and stepping by semitone or by key.
- New settings: the chosen target, and whether a score replaces it. Both are
  local.
- No audio-thread change.
