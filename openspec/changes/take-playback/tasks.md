## 1. Material

- [x] 1.1 `PlaybackEvent` velocity; `takePlaybackOf` (resampling, pedal-aware
      notes through `NoteSoundingTracker`, the start frame). Tests.

## 2. The clock

- [x] 2.1 `ClockRenderer`: the take's sound in the slot, mixed in each span.
      Tests block by block, and the RealtimeSanitizer path.
- [x] 2.2 `PracticeClock::playTake`, `playingTake`, `endTakePlayback`: the
      table built for the take, settings kept and put back on stop. Tests.
      Named `playRecording`, `playingRecording` and `endRecordingPlayback`,
      taking a `PracticeClock::Recording` (scale, start, end, keyboard events,
      sound) rather than a `take::Take`: `transport` stays below `take`, and
      the shell turns a take into a recording with `take::takePlaybackOf`.

## 3. Export

- [ ] 3.1 `library::renderTakeAudio` and a WAV writer, on the worker. Tests.

## 4. The interface

- [ ] 4.1 Take Review: Play, Stop, Export Audio…; the Library: Play, Export
      Audio…; recording or a new score ends playback. Captures; checked on a
      virtual display.

## 5. Docs

- [ ] 5.1 The wiki (Tools § Take Review, Library), ARCHITECTURE.md § Takes,
      audio-thread-safety.md.
