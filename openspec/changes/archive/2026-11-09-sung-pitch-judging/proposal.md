## Why

A keyboard take is judged right, wrong, missed, extra, early or late. A sung
take is only judged for timing: every note it matched is "played", because a
microphone onset has no pitch (`NoteOutcome::pitchNotJudged`). The MVP's loop
promises a singer "how sharp or flat" (#13), and #42 asks for exactly that:
- signed cents over each note;
- attack, sustain and release told apart, so a scoop into a note does not
  decide its verdict;
- the application's reference pitch, and a tolerance the singer sets;
- unsure frames left out, and a note with too few sure ones reported as
  uncertain rather than wrong;
- no octave catastrophes when a harmonic is louder than the fundamental;
- results per frame and per note.

The maintainer approved designing and building it on 2026-11-08.

## What Changes

- **A pitch contour of a microphone take.** A JUCE-free pitch tracker for
  recorded audio (YIN, through the evaluation FFT), giving each frame a
  frequency and a confidence. It runs once per take, on the judging thread,
  beside the onsets.
- **Each sung note judged for intonation.** For each written note the timing
  matched to an onset:
  - its frames are split into attack, sustain and release;
  - the sure sustain frames give a median deviation in cents against the
    written pitch at the take's reference;
  - the note is in tune, sharp or flat by the tolerance, wrong when it is
    nearer another note, or uncertain when too few frames were sure.
- **Extra sung notes get a pitch**, where the contour is sure of one.
- **The reference and the tolerance.** A take records the reference pitch it
  was sung against. **Settings → Practice → In tune within ±N ¢** sets the
  tolerance (default 25 ¢), and changing it re-judges the take under review.
- **The review shows it.**
  - The Take Review list reads "G4 · 18 ¢ sharp · on time".
  - The summary counts in tune, sharp and flat notes.
  - On the Score View, a sharp or flat note has an amber ring and a small
    arrow above or below it.

## Capabilities

### New Capabilities
- `sung-pitch-judging`: the contour, the per-note intonation verdict, and how
  the review shows it.

### Modified Capabilities
- `note-matching`: a single sung note matched for timing is now judged
  correct or wrong by its pitch, rather than left unjudged.
- `take-review`: rows, summary and score marks carry intonation.

## Impact

- `src/platform/evaluation`: `PitchContour`, `Intonation`, and changes to
  `NoteMatching` and `TakeReview`.
- `Take::referenceHz`, `TakeSources::referencePitch`, and
  `TakeRecorder::setIntonation`.
- `ReviewMarks` and the Score View's marks.
- A settings key, `practice.inTuneCents`, and a slider.
- A synthetic sung take and an approved state for the capture.
- Chords from a microphone stay unjudged for pitch: one pitch tracker hears one
  note.
