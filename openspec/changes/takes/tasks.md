## 1. The take, JUCE-free (`src/platform/take/`)

- [x] 1.1 `TakeLifecycle`: states, allowed transitions and end reasons, with
      tests for every transition.
- [x] 1.2 `Take`: what is kept, duration, frame-to-tick through the kept table,
      and MIDI time to frame.
- [x] 1.3 `TakeAssembler`: contiguous blocks appended, a discontinuity
      reported. Tests.

## 2. Recording on the audio thread

- [x] 2.1 The callback hands the renderer the input the tools get (tone or
      device, after gain, none while muted); the generator too.
- [x] 2.2 `ClockRenderer`: arm and disarm, the sample FIFO and block stamps,
      and overflow counted. Tests, a `[callback]` case under RealtimeSanitizer,
      and the benchmark re-run.

## 3. The recorder and the lock

- [x] 3.1 `PracticeClock::lock` and `unlock`. Tests.
- [x] 3.2 `TakeRecorder`: start, stop, cancel; following the clock; draining
      audio and MIDI; failing on a new score, lost sources or overflow; keeping
      takes. Headless tests with blocks driven by the test.
- [x] 3.3 Owned by `MainComponent`, in `ToolServices`.

## 4. Controls and verification

- [x] 4.1 Record in the transport strip, the take's status, and locked
      controls greyed (the strip, and click to seek in the Score View).
- [x] 4.2 An approved state recording a take from the synthetic tone, and its
      capture surface. Read the pictures.
- [ ] 4.3 A take from a real microphone and keyboard together. **Needs a
      person.**

## 5. Docs

- [ ] 5.1 `audio-thread-safety.md` (recording in the renderer),
      `ARCHITECTURE.md` (takes, ownership), the wiki (recording a take) and the
      Features row.
