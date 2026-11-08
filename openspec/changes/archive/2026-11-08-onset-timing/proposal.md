## Why

A take (#40) records what was played, on the practice clock's timeline. Nothing
yet says whether it was in time. That is the first judgement the practice loop
gives (#13): "this note was 80 ms late". #48 (wrong and missed notes) and #52
(review on the score) are built on the same alignment of what was played to
what was written.

## What Changes

- **Expected attacks** come from the score, for a chosen part. A tie's
  continuation is not a new attack, and rests and grace notes are not judged.
  Pickups and tempo changes are handled by the clock's own table.
- **Performed onsets** come from both inputs:
  - **the keyboard**: note-ons, gathered into chords;
  - **the microphone**: a spectral-flux onset detector that is JUCE-free and
    tested on synthetic attacks to within 10 ms.
- **The onsets are aligned to the attacks in order.** A dynamic-programming
  match within a window around each attack lets repeated notes of the same
  pitch align by their timing. An attack with no onset is reported as absent,
  and an onset with no attack as extra.
- **Each attack is judged** early, on time or late, in milliseconds and in
  beats. The tolerance scales with tempo and is configurable. A weak
  microphone onset is marked uncertain rather than trusted.
- **A take's timing report** gives every attack's result and a summary: how
  many were early, on time, late or absent, and the mean and spread of the
  error.

## Capabilities

### New Capabilities

- `onset-timing`: when each note of a take began, against when it should have.

## Impact

- New JUCE-free code in `src/platform/evaluation/`, with tests. It has no user
  interface: #52 draws the results on the score.
- Out of scope:
  - pitch (#42, #48);
  - the latency #38 measures. Until then the device's stated latencies are
    used, as a parameter, so #38 can replace them;
  - a pitch-based onset for legato singing, which #42's pitch segmentation will
    add as further evidence.
