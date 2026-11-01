## 1. The service can decline an input device

The correctness core: after this section no code path in a declined instance can
open a device, and that is enforced at the one funnel rather than at each caller.

- [ ] 1.1 Add a construction-time `bool inputDeclined` to `AudioInputService`,
      stored `const`, defaulting to false so every existing construction site is
      unaffected.
- [ ] 1.2 Early-return in `startDeviceWork()` when declined, with a comment
      saying this is the guard that makes the invariant hold.
- [ ] 1.3 Early-return in `scanForDeviceChanges()` when declined, with a comment
      saying this one is to stop the timer queueing work that would be refused,
      and is *not* what enforces the invariant.
- [ ] 1.4 Initialise the device manager with zero input channels when declined,
      so device enumeration still works and the Settings chooser still lists
      devices.
- [ ] 1.5 Leave `updateToneSource()` alone, and say in its comment why it needs
      no change: `deviceRunning` is never true in a declined instance.

## 2. Tests for the service

- [ ] 2.1 `AudioInputServiceTests.cpp`: a declined instance asked to initialise
      opens no device and reports none running.
- [ ] 2.2 A declined instance asked for a tone delivers samples to a registered
      consumer, with a sample rate and channel count, and reports input as
      working — the `synthetic-input` requirement, now reached by declining
      rather than by failing to open.
- [ ] 2.3 A declined instance whose timer ticks with no device present attempts
      no recovery and reports none in flight.
- [ ] 2.4 `resetToDefaultInput()` and `applySavedDeviceState()` on a declined
      instance are refused without opening anything, and without wedging.
- [ ] 2.5 Confirm the existing RealtimeSanitizer coverage still passes: the
      callback is untouched, so this is a regression check, not new coverage.

## 3. The flag

- [ ] 3.1 Parse `--no-audio-input` in `src/bootstrap/main.cpp` beside
      `--mute-microphone`, and thread it through `MainWindow` to the service.
      Keep it independent of `--test-control`.
- [ ] 3.2 Document it wherever `--mute-microphone` and `--test-control` are
      documented, including `docs/development/quality/TESTING_SUITE.md`.

## 4. The harness passes it and stops routing on input

A net deletion. Each item here removes something that existed only because
workers contended for the device.

- [ ] 4.1 `driver.py`: pass `--no-audio-input` alongside `--test-control`.
- [ ] 4.2 `capture.py`: delete `needs_input()`, the `has_input` attribute, and
      the `can_analyse` branch of `next_group()`, so a worker takes the next
      group unconditionally.
- [ ] 4.3 `runner.py`: delete the `wait_for_input()` probe and the
      `made.has_input` assignment, and update the `_capture_in_parallel`
      docstring — the reason parallelism works is now "no worker wants a device"
      rather than "the tone no longer needs one".
- [ ] 4.4 `__main__.py`: remove the sequential path's `has_input` assignment and
      the comment about routing tone surfaces to a worker that can hear.
- [ ] 4.5 `driver.py`: decide `has_input`/`wait_for_input` on the driver itself —
      the control channel's `status` reply still reports it and it is still worth
      having for diagnosis, so these stay; only the *routing* on them goes.
- [ ] 4.6 `python tools/scripts/run_tests.py`, and update `test_driver.py` where
      it covers the removed routing.

## 5. Verify the surfaces that depend on the device layer

The part that cannot be reasoned out. These three are the risk the proposal
names.

- [ ] 5.1 Capture `settings-audio-device` and confirm the input-device chooser
      still lists real devices and still switches, against the existing
      reference.
- [ ] 5.2 Capture `microphone-warning` and confirm a declined instance with a
      tone does not show the no-usable-input warning, since
      `synthetic-input` requires the interface not to report input as
      disconnected while a tone is feeding the tools.
- [ ] 5.3 Capture `microphone-muted` and `muted-all-tools` and confirm muting
      still reads as muted — the mute path is upstream of the tone and should be
      unaffected, which is worth a picture rather than an assumption.
- [ ] 5.4 Run a full parallel capture sweep and confirm no worker reports
      contention, and that the duplicate-capture check flags nothing new.

## 6. Prove the bug is gone

- [ ] 6.1 With a live Practice Takes open and holding the microphone, run a
      capture sweep and confirm the live application keeps its input throughout
      — this is the failure in #176 and the thing the change is for.
- [ ] 6.2 Record the result on #176 and close it, noting that the alternative —
      documenting single-instance ownership as intended — was considered and
      rejected because the contended device was never used.
