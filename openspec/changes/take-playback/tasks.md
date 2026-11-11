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

- [x] 3.1 `library::renderTakeAudio` and a WAV writer, on the worker. Tests.

## 4. The interface

- [x] 4.1 Take Review: Play, Stop, Export Audio…; the Library: Play, Export
      Audio…; recording or a new score ends playback. Captures; checked on a
      virtual display.
      Record does not end playback itself: while a take plays, Record is
      refused with "a take is playing back; stop it first", because the
      clock's position is the playback's for a block after it ends, and a take
      started then would start from the wrong place. The shell's actions reach
      Take Review as `ToolServices::playTake` and `exportTakeAudio`, beside
      `showScoreImportSummary`, so the tool still names nothing of the shell.

## 5. Docs

- [x] 5.1 The wiki (Tools § Take Review, Library), ARCHITECTURE.md § Takes,
      audio-thread-safety.md.
