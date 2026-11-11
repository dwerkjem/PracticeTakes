## Why

A take can be judged, listed and reviewed, but not heard. The recording is in
the take's folder (`microphone.flac`), and its keyboard notes are in its
manifest, but the application cannot play either back, and a take played on
the keyboard has no audio anywhere. The maintainer asked for audio saving and
playback for takes (2026-11-11).

## What Changes

- **Play a take.** The Take Review tool and the Library's Takes list get
  **Play**:
  - the take sounds from where it started, at its own tempo scale: the
    microphone as recorded, and the keyboard through the same piano the score
    plays with, with the sustain pedal held as it was;
  - the score cursor follows it, so a mistake can be heard and seen at once;
  - Pause and Stop in the transport work as for the score, and the clock stops
    where the take ended;
  - the metronome and the score's own playback are silent while a take plays;
  - the clock's settings are as they were when it stops.
- **Export a take's audio.** **Export Audio…** writes a take to a WAV file the
  reader chooses: the microphone, and the keyboard rendered through the piano,
  in stereo at the take's sample rate. A keyboard take thereby gets audio too.
- **The microphone is still saved with every take**, as now
  (`microphone.flac`); the export is for using a take outside the application.

## Capabilities

### New Capabilities
- `take-playback`: playing a take through the clock, and exporting its audio.

## Impact

- **Audio thread**:
  - `ClockRenderer` mixes a take's samples into the output, a copy of at most
    one block per block, read from a buffer the message thread prepared and
    keeps alive;
  - keyboard notes go through the existing `ScoreSynth` path with a velocity.
  - No allocation, lock or I/O is added there. The RealtimeSanitizer test
    drives the new path.
- **New**: `platform/take/TakePlayback.h` (JUCE-free: a take to playback
  material, the pedal applied through `NoteSoundingTracker`), the export
  (`platform/library/TakeExport`), and the buttons.
- **Changed**:
  - `PlaybackEvent` gains a velocity;
  - `ClockRenderer::publish` takes the take's sound beside the table;
  - `PracticeClock` gains `playTake`, `playingTake` and `endTakePlayback`.
