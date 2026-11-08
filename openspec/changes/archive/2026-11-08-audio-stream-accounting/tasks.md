## 1. Audio service

- [x] 1.1 `addStateListener` and `removeStateListener`: a message-thread list,
      told the current state on joining, never visited by the callback.
- [x] 1.2 `maximumConsumers` 8 → 16, `consumerCapacity()`. `listenerSnapshot()`
      returns the slots' listeners, then the state-only ones.
- [x] 1.3 Tests: a state-only listener takes no slot, hears a mute, and is added
      only once. There are sixteen streams.

## 2. Tools

- [x] 2.1 The tuner registers as a state-only listener and drops its
      discard-only FIFO handling.
- [x] 2.2 `usesAudioInput` → `readsAudioSamples`, false for the tuner.
- [x] 2.3 `maximumInstancesPerTool` = 6. `canOpenAnother` returns `allowed`,
      `singleInstance`, `toolLimitReached` or `noAudioStreamFree`.
- [x] 2.4 `AudioStreamUse`: per-tool counts, the remainder attributed to shared
      pitch detection, and the alert's sentence. Tested headless.

## 3. Shell

- [x] 3.1 The Tools menu shows the reason in "Open another"'s text. It is
      disabled at the per-tool limit and clickable when out of streams.
- [x] 3.2 "Open another" and `presentTool` explain a lack of streams with the
      `AudioStreamUse` sentence.
- [x] 3.3 `WorkspaceNormalizer` drops instances past the sixth of a tool.

## 4. Verification

- [x] 4.1 `PracticeTakesTests`: all 974 cases pass.
- [x] 4.2 Captured headless: muted and disconnected tuners still say so through
      the state-only listener, and two tuners still read pitch. In a driven
      session, six tuners opened, and the menu then read "Open another (6 is
      the most)", greyed out.
- [x] 4.3 The wiki, `adding-a-tool.md`, `ARCHITECTURE.md` and
      `audio-thread-safety.md` are updated.
