## Why

The application is called Practice Takes and cannot record a take. The
practice loop (#13) starts with one: play or sing a passage against the
practice clock, then see how it went. Everything after this depends on a take
existing, with the input and the score on one timeline:
- onset timing (#43);
- matching notes (#48);
- review on the score (#52);
- pitch from the microphone (#42).

The practice clock (#36) already counts frames of the audio callback, and it
publishes the (frame, time) pair a MIDI timestamp needs. This change uses both
to record.

## What Changes

- **A take is recorded from the microphone, the MIDI keyboard, or both**,
  against the open score. It runs from the start of the count-in to the end of
  the piece or to Stop, so the first note is never clipped.
- **Microphone audio is recorded by the clock's renderer, inside the audio
  callback**, stamped with the clock position of every block. The audio and the
  clock share one timeline exactly, by construction.
- **Keyboard events are placed on the same timeline** through the clock's
  (frame, time) pair.
- **A take has an explicit lifecycle**: ready, counting in, performing,
  paused, then completed, stopped early, cancelled or failed. A take that didn't
  reach the end is never mistaken for one that did.
- **Nothing can change under a take.** While one is recorded, the tempo
  scale, the loop and seeking are locked. Opening another score, or losing
  every source, ends it as failed, keeping what it has, with the reason.
- **Record** in the transport strip starts a take and finishes it, and says
  what it is doing: counting in, recording and for how long, from which
  sources, and how the last take ended.
- **Takes are kept for the session** (#39 will save them), each with what a
  later analysis needs:
  - the score, the start tick and the tempo scale;
  - the clock's segment table, to turn frames into ticks;
  - the sample rate and the device's stated latencies, for #38.

## Capabilities

### New Capabilities

- `takes`: recording a take, its lifecycle, and what it keeps.

### Modified Capabilities

- `practice-clock`: the renderer may record the input it is handed while a
  take is recorded, and the clock can be locked against changes during one.

## Impact

- `src/platform/audio/AudioInputService.cpp`: the renderer is handed the input
  the tools get (the tone or the device, after mute) rather than the raw device
  input.
- `src/platform/transport/`:
  - `ClockRenderer` gains a recording FIFO and block stamps;
  - `PracticeClock` gains a lock.
- `src/platform/take/` (new):
  - `Take.h` and `TakeLifecycle.h`, JUCE-free;
  - `TakeAssembler` (stamped blocks into one timeline), JUCE-free;
  - `TakeRecorder`, the message-thread service.
- `MainComponent` owns the recorder. It is declared after the clock and the
  MIDI service and before the live tools, and `ToolServices` carries it.
- The transport strip: Record, and the take's status.
- Out of scope:
  - analysing a take (#42, #43, #48);
  - showing one on the score (#52);
  - saving takes (#39);
  - accompaniment (#286);
  - choosing a part or track to be judged against, which the analyses need and
    #48 decides.
