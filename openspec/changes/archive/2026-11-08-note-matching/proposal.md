## Why

Onset timing (#43) says when each moment of a take began against the score.
It doesn't say whether the right notes were played. #48 asks which expected
notes were played, which were missed, which were wrong, and which played notes
were extra. That is the second judgement the practice loop gives (#13), and
the alignment every later per-note metric reads (#44, #45, #52).

## What Changes

- **Note matching** on top of #43's moment alignment. Matched moments compare
  pitch sets:
  - a note played at its pitch is **correct**;
  - a note of the moment replaced by another pitch is **wrong**, keeping what
    was played;
  - an expected note not played is **missed**;
  - a played note with no expected note is **extra**.

  An unmatched moment's notes are all missed, and an unmatched onset's notes
  are all extra.
- **Ambiguity is kept.** When a played pitch could be the wrong note for two
  expected notes of a chord, the nearest pitch is chosen and the result is
  marked ambiguous.
- **One alignment serves both inputs.** The keyboard's onsets carry exact
  pitches. The microphone's carry none until #42's pitch segmentation, so a
  sung take's notes are reported as **pitch not judged** rather than as wrong.
  #42 fills those pitches in without changing this code.
- **The report is readable as test output**: every expected note with its
  outcome and timing, and every extra note.

## Capabilities

### New Capabilities

- `note-matching`: which notes of a take were right, wrong, missed or extra.

## Impact

- New JUCE-free code in `src/platform/evaluation/` (`NoteMatching`), with
  tests. It has no user interface: #52 draws it on the score.
- Out of scope:
  - pitch from the microphone (#42);
  - duration and the pedal (#44, #203), which add evidence later without
    changing this shape.
