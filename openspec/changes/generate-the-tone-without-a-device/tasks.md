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
- [ ] 5.2b Still to run: the `tsan` suite, which is the one that matters for the token
- [x] 5.2 Captured the tone surfaces at six workers and opened them: six harmonics, moving formant bands
- [x] 5.3 Measured, hub full sweep: **178s at one worker, 97s at six** — the surfaces carrying a
      tone are no longer queued behind whichever worker won the device
- [ ] 5.4 Still to do: confirm an ordinary run with a real microphone is unchanged

## 6. A tone worth photographing

- [x] 6.1 Six partials rather than two — an analyser with two bars looks the same working well or barely
- [x] 6.2 A formant that travels, so the spectrogram shows movement rather than parallel lines
- [x] 6.3 Headroom corrected: charging every partial the full formant boost quietly halved the signal,
      which the amplitude test caught

- [x] 6.4 The hub captures on several screens by default — it was still running one pass, which is
      where a sweep is actually started from
