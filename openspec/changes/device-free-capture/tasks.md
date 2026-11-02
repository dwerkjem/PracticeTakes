## 1. The service can decline an input device

The correctness core: after this section no code path in a declined instance can
open a device, and that is enforced at the one funnel rather than at each caller.

- [x] 1.1 Add a construction-time `bool inputDeclined` to `AudioInputService`,
      stored `const`, defaulting to false so every existing construction site is
      unaffected.
- [x] 1.2 Early-return in `startDeviceWork()` when declined, with a comment
      saying this is the guard that makes the invariant hold.
- [x] 1.3 Early-return in `scanForDeviceChanges()` when declined, with a comment
      saying this one is to stop the timer queueing work that would be refused,
      and is *not* what enforces the invariant.
- [x] 1.4 Initialise the device manager with zero input channels when declined,
      so device enumeration still works and the Settings chooser still lists
      devices.
- [x] 1.5 Leave `updateToneSource()` alone, and say in its comment why it needs
      no change: `deviceRunning` is never true in a declined instance.

## 2. Tests for the service

- [x] 2.1 `AudioInputServiceTests.cpp`: a declined instance asked to initialise
      opens no device and reports none running.
- [x] 2.2 A declined instance asked for a tone delivers samples to a registered
      consumer, with a sample rate and channel count, and reports input as
      working — the `synthetic-input` requirement, now reached by declining
      rather than by failing to open.
- [x] 2.3 A declined instance whose timer ticks with no device present attempts
      no recovery and reports none in flight.
- [x] 2.4 `resetToDefaultInput()` and `applySavedDeviceState()` on a declined
      instance are refused without opening anything, and without wedging.
- [x] 2.5 Confirm the existing RealtimeSanitizer coverage still passes: the
      callback is untouched, so this is a regression check, not new coverage.

## 3. The flag

- [x] 3.1 Parse `--no-audio-input` in `src/bootstrap/main.cpp` beside
      `--mute-microphone`, and thread it through `MainWindow` to the service.
      Keep it independent of `--test-control`.
- [x] 3.2 Document it wherever `--mute-microphone` and `--test-control` are
      documented, including `docs/development/quality/TESTING_SUITE.md`.

## 4. The harness passes it and stops routing on input

A net deletion. Each item here removes something that existed only because
workers contended for the device.

- [x] 4.1 `driver.py`: pass `--no-audio-input` alongside `--test-control`.
- [x] 4.2 `capture.py`: delete `needs_input()`, the `has_input` attribute, and
      the `can_analyse` branch of `next_group()`, so a worker takes the next
      group unconditionally.
- [x] 4.3 `runner.py`: delete the `wait_for_input()` probe and the
      `made.has_input` assignment, and update the `_capture_in_parallel`
      docstring — the reason parallelism works is now "no worker wants a device"
      rather than "the tone no longer needs one".
- [x] 4.4 `__main__.py`: remove the sequential path's `has_input` assignment and
      the comment about routing tone surfaces to a worker that can hear.
- [x] 4.5 `driver.py`: decide `has_input`/`wait_for_input` on the driver itself —
      the control channel's `status` reply still reports it and it is still worth
      having for diagnosis, so these stay; only the *routing* on them goes.
- [x] 4.6 `python tools/scripts/run_tests.py`, and update `test_driver.py` where
      it covers the removed routing.

### Left for a follow-up, deliberately

With no worker opening a device, three more pieces of the harness have nothing
left to guard, but removing them is a wider change than this one and is not
covered by the proposal:

- the **audio gate** (`Shared.audio`), which serialises worker startup so that
  one application acquires the device at a time. Also taken in
  `capture.py:386`, so it is not purely a startup concern and needs reading
  before it goes.
- the **contention warning** printed from `fleet_module.contention_warning`,
  which tells the operator which instances the run is competing with.
- `STARTUP_ATTEMPTS` and `_start_under`'s retry, which exist because "the
  application reopens the device from a timer of its own ... and that can land
  while another worker holds it".

These are inert rather than wrong, so they are left working and noted. Raise
them as their own issue once section 6 has confirmed the contention is actually
gone.

## 5. Verify the surfaces that depend on the device layer

The part that cannot be reasoned out. These three are the risk the proposal
names.

Taken, and they found two regressions the reasoning had not: declining the
device changed what two of these three surfaces are pictures *of*. Both are
fixed below. This is why the section exists.

- [x] 5.1 `settings-audio-device`. **Regressed, then fixed.** A declining
      instance showed `Input: << none >>`, no channel list and no advanced
      settings, and its question — whether switching device takes effect —
      could only answer no, since a declined instance refuses device requests
      by design. Fixed with a per-surface `needs_input_device` declaration:
      the worker restarts into an instance with a device for that surface and
      back out after, under the audio gate, so the microphone is held for one
      surface instead of the whole run. Recaptured and it matches the
      reference: device selected, channel list and advanced settings present.
- [x] 5.2 `microphone-warning`. **Correct, and better than before.** It shows
      "No microphone detected" with Open Settings and Dismiss, which is what
      the surface is for — and it no longer depends on the capture machine
      happening to have no microphone.
- [x] 5.3 `microphone-muted`. **Regressed, then fixed.** It showed
      *disconnected*, not muted: `inputState()` only reports `muted` when
      `hasUsableInput()` is true, and a declining instance with no tone has no
      input, so the capture was a picture of disconnection under a mute
      surface's name. Fixed by giving `microphone-muted` and `muted-all-tools`
      a tone. That exposed a second bug — `renderToneBlock()` never checked
      `muted`, because mute was honoured in the audio callback and the
      generator bypasses it, so **muting a device-free instance silenced
      nothing**. Latent until every capture worker became device-free. Fixed
      and covered by a test; the recapture shows the amber "Mic muted" badge,
      "Microphone muted." and an empty graph.
- [ ] 5.4 Run a full parallel capture sweep and confirm no worker reports
      contention, and that the duplicate-capture check flags nothing new.

## 6. Prove the bug is gone

- [x] 6.1 Confirmed by reading `/proc/<pid>/fd` for ALSA capture nodes
      (`pcmC*D*c`) rather than by eye, with three worker instances started
      first and the live application opened after them -- the order that
      actually bites, and the one #176 describes as "the run works; the live
      application is the one that ends up without input".

      | workers | workers hold | live application gets |
      |---|---|---|
      | 3, no flag | `pcmC3D0c` | **nothing** -- the bug |
      | 3, `--no-audio-input` | nothing | `pcmC3D0c` |

      A single instance was checked the same way: without the flag it holds
      `pcmC3D0c`, with it only `seq` -- the MIDI sequencer, which owns its own
      `juce::MidiInput` and is correctly untouched.
- [ ] 6.2 Record the result on #176 and close it, noting that the alternative —
      documenting single-instance ownership as intended — was considered and
      rejected because the contended device was never used.
