## Why

One process at a time can hold the input device. Issue #176 measured it: with
eight capture workers running, seven report no input. `audio-device-recovery`
and the device-free tone made that survivable — the interface no longer freezes
on a contended device, and a worker that cannot open one can still photograph a
surface carrying a tone — but the contention itself is still there, and a
capture run and a live application still compete. The run wins; the person with
the application open is the one who ends up without a microphone.

The reason to fix it rather than document it is that **a capture worker does not
want the device it is fighting for**. The audio callback already replaces the
device's input with the generated tone whenever a tone is asked for
(`AudioInputService.cpp:479-492`), so a worker that wins the microphone opens
it, reads from it, and throws every sample away. The contention buys nothing.

It also costs something beyond the microphone. Because `updateToneSource()`
gates the generator on `!deviceRunning`, "has this worker got input" is still a
property worth routing on, so `capture.py` carries a `has_input` flag, a
`needs_input` predicate, a worker-selection rule, and a comment conceding the
whole thing is "mostly moot now that a tone is generated without a device". That
machinery exists only to work around the contention.

## What Changes

- **An instance can decline an input device.** A new `--no-audio-input` flag
  means: do not open an input device, do not attempt recovery, and do not scan
  for device changes. The generated tone becomes the only producer.
- **Devices are still enumerated.** Declining to *open* a device is not
  declining to *know about* them. The Settings device list must stay populated,
  because `settings-audio-device` is captured with a human instruction to switch
  input device and back. Enumeration does not hold the PCM; opening does.
- **The capture harness passes it.** `driver.py` adds the flag next to
  `--test-control`, so every capture worker is device-free by construction.
- **The `has_input` routing goes.** With no worker holding a device and the tone
  always available, every worker can honestly capture every surface. The flag,
  the predicate, the worker-selection rule and the startup `wait_for_input`
  probe are deleted rather than left as dead conditionals.
- **`--test-control` alone is unchanged**, so a human can still drive a real
  device through the channel when debugging. The flag is explicit for that
  reason.

### Non-goals

- **Two instances sharing one microphone.** Still impossible, still not wanted.
  Nothing in the product needs it; this change removes the one place that
  wanted it by accident.
- **Changing the audio-thread callback contract.** The callback is untouched.
  What changes is whether a device is ever opened, which is a message-thread and
  background-thread concern.
- **Changing what the tone sounds like**, or which surfaces ask for one.

## Capabilities

### Modified Capabilities

- `synthetic-input`: adds requirements for an instance that declines an input
  device outright, and for the generated tone being the sole producer in that
  instance. The existing requirements are unchanged — this is a new way to
  arrive at "a tone with no device", not a change to what that means.
- `audio-device-recovery`: adds a requirement that an instance which has
  declined input attempts no recovery and scans for no device changes. The
  existing recovery requirements are unchanged and continue to govern every
  ordinary run.

## Impact

- **Changed**: `AudioInputService` grows a construction-time "input declined"
  state that `initialiseInput()`, `scanForDeviceChanges()` and
  `attemptRecovery()` honour. It is deliberately not a runtime setting — an
  instance is a capture instance or it is not, and a flag that could flip
  mid-run would need the device-open paths to be cancellable, which
  `audio-device-recovery` specifically decided against.
- **Changed**: `src/bootstrap/main.cpp` parses `--no-audio-input` and threads it
  through `MainWindow` to the service, alongside the existing
  `--mute-microphone`.
- **Changed**: `tools/scripts/testing_suite/driver.py`, `capture.py` and
  `runner.py` — the flag is passed and the routing is removed. This is a net
  deletion in the harness.
- **Unchanged**: every surface definition in `surfaces.py`. The set of pictures
  a run takes does not change; what changes is that any worker can take any of
  them.
- **Risk**: `settings-audio-device`, `microphone-warning` and
  `microphone-muted` are the surfaces whose content depends on the device layer.
  They must be captured and compared against the existing references before this
  is called done — an empty device chooser would be a quieter regression than
  the bug being fixed.
- **Blocked by**: nothing.

## Long-term compatibility

The flag is a verification-harness affordance, not a user-facing feature, so
there is no settings key and nothing persisted. The lasting commitment is the
narrower one: that **declining input is a thing an instance can do at all**.
Once the harness relies on it, an ordinary run and a capture run differ in
whether a device is ever opened, and that difference has to stay visible in the
service rather than becoming an implicit property of being under
`--test-control`. Keeping the flag separate from `--test-control` is what keeps
it visible.
