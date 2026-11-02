## Context

`AudioInputService` owns the single `AudioDeviceManager`'s input side. Three
paths open a device:

- `initialiseInput()` at startup and on an explicit user choice
  (`AudioInputService.cpp:320`),
- `scanForDeviceChanges()` on the service's timer, which calls
  `attemptRecovery()` when the device it expected has gone,
- `startDeviceWork()`, the shared entry both of the above funnel through, which
  runs the blocking open on a detached background thread
  (`AudioInputService.cpp:763-778`).

Opening is blocking and, when another process holds the device, unbounded —
`#176` records the ALSA/pipewire stack wedged in `pthread_cond_wait` with no
timeout to reach. `audio-device-recovery` is the spec that decided the message
thread never waits on it and that the thread is detached rather than joined,
precisely so a hung open harms nothing.

Two facts make this change small:

1. **The tone already wins over the device.** The audio callback renders the
   tone into `toneBlock` and points `inputSamples` at it whenever
   `toneFrequency > 0` (`AudioInputService.cpp:479-492`). A capture worker
   holding a microphone analyses the tone, not the microphone.
2. **The tone already runs without a device.** `ToneSource` is a
   `HighResolutionTimer` that fills the analysis FIFOs directly, started by
   `updateToneSource()` when a tone is wanted and `!deviceRunning`
   (`AudioInputService.cpp:386-408`).

So the device in a capture instance is not load-bearing. It is contended for and
discarded.

## Goals / Non-Goals

**Goals**

- A capture instance never opens an input device, so a capture run cannot take
  the microphone from a live application.
- Every capture worker can capture every surface, because none of them is
  privileged by having won a device.
- The decision is visible in the service and in the command line, not inferred
  from `--test-control`.

**Non-Goals**

- Sharing one device between processes.
- Making a device open cancellable. `audio-device-recovery` decided against it
  and nothing here needs it — declining is a decision made before any open is
  attempted, which is why it can be construction-time.
- Touching the real-time callback.

## Decisions

### 1. Declining input is construction-time state, not a runtime setter

`AudioInputService` takes the decision as a constructor argument and stores it
`const`. A setter would imply an instance can change its mind, and changing its
mind *towards* declining would mean cancelling an in-flight open — the thing
`audio-device-recovery` explicitly does not support, because the open is a
blocking JUCE/ALSA call on a detached thread.

Const also makes the guard cheap and obviously correct: there is no ordering
question about a flag that cannot change, so the three open paths need a plain
early return and no atomic.

### 2. The guard goes at `startDeviceWork()`, with the scan stopped separately

`startDeviceWork()` is the one funnel every open passes through, so one return
there is sufficient to guarantee no device is ever opened. That is the
correctness guard and it is where the invariant is enforced.

But `scanForDeviceChanges()` runs on a timer and would otherwise keep noticing a
missing device and queueing work that is then refused, once per tick, forever.
So the scan returns early too. That one is for quiet, not correctness — which is
worth stating in the comment, so a later reader does not remove the
`startDeviceWork()` guard on the grounds that the scan already covers it.

### 3. The device manager is still initialised, with zero input channels

`juce::AudioDeviceManager::initialise(0, outputs, ...)` sets up the device types
and enumeration without requesting an input. Device *names* then still populate
the Settings chooser, which `settings-audio-device` needs: that surface is
captured with the human instruction "switch to a different input device, then
back", and an empty list makes it unanswerable.

This is the one place the change could regress a capture rather than improve
one, so it is called out in tasks as something to verify against the existing
reference images rather than assume.

### 4. `updateToneSource()` needs no change

Its condition is `tone wanted && !deviceRunning`. In a declined instance
`deviceRunning` is never true, so the generator starts whenever a tone is asked
for. Leaving the condition alone keeps one rule — "the device wins when there is
one" — rather than adding a second that happens to agree with it.

### 5. The flag is separate from `--test-control`

`--test-control` opens the command channel. `--no-audio-input` declines the
device. They travel together from `driver.py`, but a human debugging through the
channel with a real microphone is a thing that should stay possible, and
conflating them would remove it silently.

### 6. The harness routing is deleted, not disabled

`has_input` on the capture pass, `needs_input()`, the `can_analyse` worker
selection in `next_group()` and the `wait_for_input()` probe in `runner.py` all
exist to keep tone surfaces away from device-less workers. With no worker having
a device and the tone always available, the predicate is constant. A constant
predicate left in place reads as a live rule and will be maintained as one, so
it goes. The reasoning it carries — that a tone surface photographed without a
signal is the worst kind of wrong, because it counts as captured — moves into
the spec, where it is the justification for the tone always being available.

## Risks

- **A declined instance that silently declines when it should not.** If
  `--no-audio-input` ever reached an ordinary run, the application would be deaf
  with no warning. Mitigated by it being opt-in, absent from settings, and
  passed from exactly one place in the harness.
- **`microphone-warning` may become unreachable or permanent.** That surface
  shows the no-usable-input warning. Whether a declined instance should show it
  is a product question: the honest answer is that a declined instance *has*
  input, because the tone is feeding the tools and
  `synthetic-input` already requires the interface not to call that
  disconnected. So the warning should behave as it does with a tone today. This
  needs checking against the reference capture, not reasoning alone.
