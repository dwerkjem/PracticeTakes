## Why

**The interface freezes when opening the input device blocks, and there is no
timeout and no way back.**

`AudioInputService::scanForDeviceChanges` runs on a timer on the message thread.
When it decides there is no usable input it reopens the device there — and
`snd_pcm_prepare` through PipeWire waits on a condition variable that another
holder of the device never signals. The message thread never returns. Nothing
repaints, no menu opens, and the test control channel — which is answered by
work posted to that thread — goes silent.

Read off a stack, not inferred:

```
AudioInputService::timerCallback         (AudioInputService.cpp:451)
 → scanForDeviceChanges                  (AudioInputService.cpp:569)
   → AudioDeviceManager::initialise
     → ALSAAudioIODevice::open → snd_pcm_prepare
       → libasound_module_pcm_pipewire → pw_thread_loop_wait → pthread_cond_wait
```

The scan runs every two seconds while there is no usable input, so a machine in
that state retries into the same wait indefinitely.

**It is already costing something concrete.** Verification captures cannot run
while anybody has Practice Takes open: with a second instance holding the
device, a capture pass — one worker, the way it has always run — captures
nothing at all, because the application it drives stops answering on its first
command. That is not a new regression. It has been true the whole time and was
invisible, because the failure was a wait on a pipe with nobody to report it.

For a user it is smaller but worse: unplug an interface, or restart PipeWire,
and the window stops responding. There is no message, and quitting means
killing it.

## What Changes

- **Reopening the input device happens on a thread of its own.** The message
  thread asks for a recovery and carries on; it never waits for a device to
  open.
- **The device is asked for once at a time.** A recovery already in flight is
  not started again by the next tick, which is what turns a slow open into a
  queue of them.
- **A recovery that does not finish is visible.** The service already publishes
  an input state; "trying to open the input device" becomes one of the things
  it can say, so a stuck open is something the interface reports rather than
  something it becomes.
- **Explicit device changes keep working as they do.** Choosing a device in
  Settings goes through JUCE's own selector on the message thread. Somebody
  asked for that and is watching it; a background retry is neither.

## Non-goals

- **Not a change to the audio callback.** It stays `noexcept`,
  `PRACTICE_TAKES_NONBLOCKING`, and does exactly what it does now.
- **Not multi-instance audio.** Two instances sharing one input device is a
  separate question. This makes contention survivable, not absent.
- **Not a rewrite of the recovery rule.** `AudioRecoveryPolicy` decides *whether*
  to recover and is already tested; this changes *where* the recovery runs.
- **No new dependency, and no change to what a capture captures.**

## Capabilities

### Added Capabilities

- `audio-device-recovery`: how the application regains a working input device
  after losing one, and what it must not block while trying.
