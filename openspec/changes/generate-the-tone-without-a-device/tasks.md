## 1. One place that delivers samples

- [x] 1.1 Extract "measure the peak, push to every consumer" from the callback, so both producers use it
- [x] 1.2 Keep it non-blocking: it is still called from the audio thread

## 2. A source that does not need a device

- [x] 2.1 Render blocks at the nominal rate on a source of its own when a tone is asked for
- [x] 2.2 Start it when a tone is set and no device is delivering; stop it when one is, or the tone goes
- [x] 2.3 Publish the same lifecycle a device does, so a tool is told its rate and channel count

## 3. Only one producer

- [x] 3.1 A token both producers take before filling a FIFO, so a device starting mid-block cannot race the source
- [x] 3.2 The callback's half stays non-blocking — an exchange, not a lock
- [x] 3.3 Test: samples arrive from the source with no device open

## 4. What a tool sees

- [x] 4.1 A tone with no device reports as input rather than as disconnected
- [x] 4.2 Test: the state a tool is told matches the samples it is given

## 5. Verification

- [x] 5.1 `ctest` green at 493
- [x] 5.2b Ran the `tsan` suite against the token, and it was not clean the first time: a real data
      race, `setSyntheticTone`'s `tone.reset()` racing `renderToneBlock`'s `SyntheticTone::advance()`
      on the tone source's own timer thread whenever a tone changes while one is already playing --
      which the test-control channel does on every `open-state`, so any two tone-bearing surfaces
      captured back to back hit it. Fixed: `setSyntheticTone` now stops the tone source
      unconditionally before touching `tone`, using the same `stopTimer()`/callback-mutex ordering
      `updateToneSource` already relied on. Regression test added and verified in both directions
      (reverting the fix reproduces the exact TSan report). 233 assertions across every
      audio/tone/recovery/load case now pass clean under ThreadSanitizer.
- [x] 5.2 Captured the tone surfaces at six workers and opened them: six harmonics, moving formant bands
- [x] 5.3 Measured, hub full sweep: **178s at one worker, 97s at six** — the surfaces carrying a
      tone are no longer queued behind whichever worker won the device
- [x] 5.4 Confirmed headlessly against a real capture device (this machine has one), so nothing
      needed to appear on a screen: launched the control-enabled build on a private Xvfb display,
      opened `tuner-docked` with no tone requested, and polled `status` every 500ms for 15s. The
      device path reported `input` on the very first poll and held it for the whole window --
      never `opening`, never a lost `input`. Clean exit, no stray process or display afterwards.

## 6. A tone worth photographing

- [x] 6.1 Six partials rather than two — an analyser with two bars looks the same working well or barely
- [x] 6.2 A formant that travels, so the spectrogram shows movement rather than parallel lines
- [x] 6.3 Headroom corrected: charging every partial the full formant boost quietly halved the signal,
      which the amplitude test caught

- [x] 6.4 The hub captures on several screens by default — it was still running one pass, which is
      where a sweep is actually started from
