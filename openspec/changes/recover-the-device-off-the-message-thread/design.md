## Context

**Where the manager is mutated.** Every call that opens, closes, or reconfigures
the device is in one file, plus one component nobody here wrote:

| Site | Thread | Who asked |
| --- | --- | --- |
| `initialiseInput` (`:272`) | message | startup, and a settings import |
| `scanForDeviceChanges` (`:560-569`) | message, on a timer | **nobody — this is the one that hangs** |
| destructor (`:56`) | message | shutdown |
| `AudioDeviceSelectorComponent` | message | the person choosing a device |

Only the second runs unasked, and only the second is a background retry into a
call that can wait forever.

**How often.** `disconnectedDeviceScanIntervalTicks` is 2 seconds and
`connectedDeviceScanIntervalTicks` is 15. So the state that hangs — no usable
input — is also the state that retries most often.

**What the scan does, in order:** enumerate every device type, ask
`AudioRecoveryPolicy` whether to recover, and if so close the current device and
reopen it. The enumeration can block too; the open is what has been observed
blocking.

**`AudioRecoveryPolicy` is already separate and already tested.** It is a pure
function over four booleans, in its own header, with its own test file. The
decision is not what is being changed.

## Goals / Non-Goals

**Goals**

- The message thread never waits on a device open it did not ask for.
- One recovery attempt at a time, however long one takes.
- A recovery that is stuck is something the interface can say.

**Non-Goals**

- Changing when a recovery is attempted.
- Making `AudioDeviceManager` thread-safe in general.
- Anything about the audio callback.

## Decisions

### 1. A thread that owns the recovery, not one that owns the manager

The tempting design is a device thread that owns every mutation of
`AudioDeviceManager`, with the message thread posting to it. It is rejected
because it cannot be true: `AudioDeviceSelectorComponent` is JUCE's own
component, it mutates the manager directly from the message thread, and it is in
the Settings window. A design whose invariant is "only this thread touches the
manager" would be false the first time somebody opened Settings, and an
invariant that is false in one place is not an invariant.

So the narrower claim, which can be kept: **the automatic recovery runs on its
own thread, and is the only thing on it.** `AudioDeviceManager` guards itself
with an internal lock; what this removes is not concurrent access but the
message thread *waiting* on it.

*What this costs:* choosing a device in Settings can still block the message
thread, because that goes through JUCE's selector. That case has somebody
watching who asked for it, which is the whole difference from a two-second
timer.

### 2. Ask, do not wait

The timer's job becomes: decide whether a recovery is wanted, and if one is not
already running, ask for one. It never joins, never polls for completion, and
never holds a lock the recovery thread might hold.

### 3. One at a time, and the flag is the gate

`recovering` already exists as a bool guarding re-entry. It becomes the thing
the timer checks before asking, and it is set and cleared on the recovery
thread. An atomic, because two threads read it now.

The order matters: set before starting the work, cleared after it finishes, and
the timer treats "already recovering" as "nothing to do". A recovery that never
finishes therefore stops further attempts rather than queueing them — which is
the behaviour wanted, since attempt two would wait on exactly what attempt one
is waiting on.

### 4. Publish "trying" as a state, not a spinner

The service already publishes an input state that the shell renders. A recovery
in flight is a state the interface can show, and a recovery in flight for a long
time is the thing a person needs told: it is the difference between "no
microphone" and "the microphone is not answering".

*What this rules out:* a modal, a progress bar, or anything that has to be
dismissed. The window stays usable — that is the point of the change.

### 5. Shutdown has to be able to happen during a stuck open

The recovery thread may be inside a call that never returns. The destructor
cannot join it, or quitting would hang exactly as the interface does now.

The thread is therefore detached-in-effect: shutdown signals it, stops using its
results, and does not wait. That is safe only if the thread touches nothing that
has been destroyed, which constrains what it may hold — a reference to the
manager and to nothing else, with everything it reports going through an atomic.

*This is the risky part of the change* and is where the tests should be
concentrated.

### 6. What a test can actually prove here

None of this can be tested against a real ALSA device that hangs on demand. What
can be tested is the shape:

- the timer asks and returns, given a recovery that blocks
- a second tick during a stuck recovery asks for nothing
- the state the service publishes says a recovery is in flight
- a service destroyed during a stuck recovery does not block and does not touch
  anything it has destroyed

That means the recovery step has to be injectable — a seam where the real one
calls the manager and a test one blocks on an event. The seam is worth having
for its own sake: it is also what makes "what does this do when the device never
opens" answerable at all.

## Risks / Trade-offs

**A detached thread inside a blocking call outlives the service.** It has to,
or shutdown hangs. What makes it safe is that it holds only the manager
reference and reports only through atomics — and the manager outlives it,
because it is owned by the shell above. If that ownership ever inverts this
becomes a use-after-free, which is worth a comment at the declaration rather
than only here.

**Two threads can now be inside `AudioDeviceManager` at once** — the recovery
thread and the Settings selector. JUCE guards the manager with its own lock, so
this is serialised rather than concurrent, but it means choosing a device while
a recovery is stuck will block Settings. Better than today, where it blocks
everything, and honest to say out loud.

**The recovery thread can be started while the previous one is stuck**, if the
flag were ever cleared wrongly. One flag, set and cleared in one place, on one
thread.

## Migration Plan

1. Extract the recovery step behind a seam, with the current behaviour on the
   message thread. No functional change; tests for the seam.
2. Move it to its own thread, with the one-at-a-time flag.
3. Publish the in-flight state and render it.
4. Verify by holding the device from another process and confirming the window
   stays usable — the case that is currently a freeze.

## Open Questions

- **Should a stuck recovery eventually give up and say so permanently?** There
  is no way to cancel a blocked ALSA open, so "giving up" can only mean
  abandoning the thread and starting another later — which is a thread leak on
  a machine that stays in that state. Leaving one stuck thread and reporting it
  is probably right, and is worth deciding before the interface is built.
- **Does the capture pass want a way to skip the device entirely?** It would fix
  the verification case directly. It is a separate change and possibly a better
  one for that purpose; this change is about the application not freezing.
