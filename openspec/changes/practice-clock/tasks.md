## 1. Timing rules, JUCE-free (src/platform/transport/ClockMath.h)

- [ ] 1.1 `ClockSegment` and `buildSegments(tempoMap, timeSignatures, scale,
      sampleRate)`: at most 512 segments, and report a truncation.
- [ ] 1.2 Frame → (bar, beat, accented) and "does a click start inside
      [frame, frame + n)?", by binary search plus one multiply.
- [ ] 1.3 Tick ↔ frame at a scale, and re-anchoring the position when the scale
      changes.
- [ ] 1.4 The loop: bounds to whole frames, and an exact wrap.
- [ ] 1.5 The count-in start frame for *n* bars at a point's tempo and meter.
- [ ] 1.6 Tests in `src/tests/platform/transport/ClockMathTests.cpp`:
      - tempo changes to the frame;
      - 7/8;
      - 10,000 wraps at 44.1 kHz with a tempo change inside the loop;
      - a scale change mid-bar;
      - a count-in in 3/4.

## 2. The real-time renderer

- [ ] 2.1 `RealtimeRenderer` and `RenderBlock` (in `src/platform/audio/`).
      `AudioInputService::setRealtimeRenderer` publishes with release, and a
      swap waits for `callbacksInProgress`.
- [ ] 2.2 The callback calls the renderer after clearing the output and before
      the mute check. `ToneSource` calls it with no output.
- [ ] 2.3 `ClockRenderer`:
      - two preallocated segment tables and an atomic index with a generation
        echo;
      - two clicks synthesised at construction;
      - a click straddling a block boundary;
      - atomic latches for wraps, seeks and the end of the piece.
- [ ] 2.4 Drive it through `AudioInputServiceCallbackAccess` in tests, under the
      RealtimeSanitizer job, and add a `[.benchmark]` case for a 512-segment
      table.

## 3. Output on the device

- [ ] 3.1 `initialise(2, 2, …)`, falling back to `(2, 0, …)` on failure, with
      the reason kept for Settings.
- [ ] 3.2 Settings → Audio: the output device, metronome volume, accent on or
      off, and "Metronome unavailable: …" when there is no output. Saved
      locally and not exported, like the MIDI keyboard.
- [ ] 3.3 Re-check audio device recovery (`audio-device-recovery` spec) with
      outputs open: unplugging the output alone must not stop capture.

## 4. PracticeClock, message thread

- [ ] 4.1 `PracticeClock`:
      - play, pause, stop, seek, count-in, scale, loop, metronome on or off;
      - the score comes from `CurrentScore`;
      - it republishes segments when the score or the scale changes.
- [ ] 4.2 A 60 Hz notification timer with `ClockListener`, `ClockPosition` and
      the latched events. Publish the (frame, high-resolution time) pair for
      #40.
- [ ] 4.3 Owned by `MainComponent`, declared after `audioInputService` and
      before `liveTools`. Passed in `ToolServices`.
- [ ] 4.4 Headless tests with a fake block source: states, seek and wrap
      events delivered once each, and the count-in reported.

## 5. Controls and verification

- [ ] 5.1 The transport strip, placed as decided in design.md's open
      question 1.
- [ ] 5.2 An approved state with the clock playing a score, plus its capture
      surface. Read the pictures.
- [ ] 5.3 On real speakers: the click is in time, and audible with the
      microphone muted. **Needs a person.**

## 6. Docs

- [ ] 6.1 `audio-thread-safety.md`: rendering the clock is a permitted callback
      activity, and how it is bounded.
- [ ] 6.2 `ARCHITECTURE.md` (the transport, and the ownership order), the wiki
      (Tools or Getting Started: the transport, the metronome, loops), and the
      Features page row for #36.
