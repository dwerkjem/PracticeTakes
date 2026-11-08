## Why

Every part of the MVP's practice loop happens against a clock:
- a take is counted in, played or sung at a chosen tempo, and judged by when
  each note landed (#40, #43, #48);
- a weak passage is looped, slower (#201);
- a singer sings with the score's other parts (#286, #37);
- the score cursor follows along (#33).

Practice Takes has no such clock. Nothing knows where in the piece you are, and
the application has never made a sound: `AudioInputService` opens its device
with no output channels, and the audio callback's first act is to clear an
output it never writes.

Issue #36 asks for that clock. It is the first audio **output** path, which
changes the audio-thread contract, and it has to be one clock that the
microphone, the keyboard and the sound coming out all agree on, or every
timing judgement built on it is off by an unknown amount. Hence this proposal,
before any code.

## What Changes

- **A practice clock** (`src/platform/transport/`), one per application,
  shared by every tool through `ToolServices`. It has:
  - play, pause, stop and seek;
  - a count-in;
  - a metronome with an accented downbeat;
  - a tempo scale (5% to 200% of the score's tempo, in 5% steps);
  - one loop region.
- **Position comes from audio samples.** It is counted in frames of the audio
  callback that also captures the microphone, so the click you hear, the note
  you sing and the clock's position share one timeline. On a device without
  output, the clock still runs and the click is silent. With no device at all,
  it runs from the synthetic-input timer.
- **The score's tempo is honoured exactly.** `TempoMap` is flattened, on the
  message thread, into a small preallocated table of tempo segments that the
  audio thread reads. The audio thread never touches the score model, as the
  model's own rules require.
- **Loops don't drift.** Loop bounds are converted to whole frames once, when
  the loop is set, and a wrap subtracts an exact frame count. Nothing
  accumulates, however many times the loop runs.
- **The device opens output.** `AudioInputService` initialises two inputs and
  two outputs on the same device (duplex), and hosts one real-time renderer
  that the callback calls after clearing the output. The renderer is
  annotated, bounded and allocation-free, under the same contract and the same
  RealtimeSanitizer job as the callback.
- **Position notifications on the message thread.** At display rate, a
  listener receives the current position in score ticks and seconds, whether
  the clock is counting in, and a discrete event when a loop wraps or a seek
  happens. The cursor (#33), takes (#40) and accompaniment (#286) all read
  this one position.

Not in scope:
- playing the score's notes (#286, accompaniment);
- recording a take (#40);
- latency compensation, which needs measured output and input latency (#38).
  The clock exposes the device's reported latencies so #38 has somewhere to
  apply its measurement.

## Capabilities

### New Capabilities

- `practice-clock`: the transport every take, cursor and accompaniment follows:
  its states, count-in, metronome, tempo scale and loop; how its position is
  derived from audio frames; and what it promises listeners about that
  position.

### Modified Capabilities

<!-- None. The audio-thread contract is documentation
     (docs/development/performance/audio-thread-safety.md), not a spec; it is
     updated in this change's tasks. -->

## Impact

- **New:** `src/platform/transport/`
  - `PracticeClock`: message-thread API and listeners;
  - `ClockRenderer`: real-time, preallocated;
  - `ClockMath`: JUCE-free segment table, beat lookup, loop wrap and count-in
    arithmetic. This is where the timing rules are tested.
- **`AudioInputService`:**
  - opens two outputs as well as two inputs;
  - hosts one `RealtimeRenderer*`, published atomically, called from the
    callback after clearing the output;
  - drives the renderer from the tone source when there's no device.
- **`ToolServices`** gains the clock. **`MainComponent`** owns it after
  `audioInputService` and before `liveTools`, which preserves the destruction
  order the ownership model relies on.
- **Settings → Audio** gains an output device, metronome volume and an
  accent toggle. The Settings window grows a transport strip, or the shell
  does (an open question).
- **Docs:** `audio-thread-safety.md` (a second permitted callback activity:
  rendering the clock), `ARCHITECTURE.md`, and the wiki.
- **Tests:**
  - `ClockMath`: segments, beats, loop wraps, count-in, and tempo-scale
    changes mid-play;
  - the renderer driven through the callback-access harness under
    RealtimeSanitizer;
  - a capture surface with the clock running.
