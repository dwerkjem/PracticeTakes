## Context

Where a run's time goes, from the code.

`SETTLE_SECONDS` is 6.0 and `capture_one` polls the window until its size stops
changing before photographing it. Surfaces carrying a tone additionally sleep
`warmup_seconds` so a tool drawing a history has one to draw. A default full run
is 204 captures. Almost all of that is one process waiting for one window.

Two checks in the pass compare captures against *other* captures:

**`duplicate_problem`** maps `(resolution, digest)` to the state that produced
it. Two different states producing identical pixels at one resolution means one
of them photographed something that was not its subject. This is not
hypothetical -- it is how the settings surface was caught coming back
byte-identical to the empty shell, because nothing named the settings window and
the X utilities took the first window the manager listed.

**`geometry_problem`** maps geometry to size, per surface and theme. A window
that reports the same size for "narrow" and "maximised" never resized, and the
capture is of the wrong thing.

Both are the kind of check that matters precisely when something is quietly
wrong, so neither may be weakened for speed.

## Goals / Non-Goals

**Goals**

- Overlap the waiting, which is nearly all of the run.
- Keep both cross-capture checks exactly as effective as they are now.
- A parallel run and a sequential run produce the same set of captures.

**Non-Goals**

- Shortening the settle time. That wait is what makes a capture trustworthy.
- Parallel `attend`.
- Choosing a worker count automatically.

## Decisions

### 1. A display and an application per worker

Each worker gets its own Xvfb display and its own application process. Sharing
one display would put two applications' windows on one screen, which is the
arrangement that made this impossible before headless existed. Sharing one
application would serialise on the control channel, which is the thing being
overlapped.

`virtual_display` already picks the lowest free display number in a private
range and checks both the socket file and a connection to it, so several at once
need no new mechanism.

### 2. Share the two maps rather than partition the plan around them

The tempting split is by theme: `geometry_problem`'s map is keyed per surface
and theme, and `duplicate_problem`'s comparisons are within a resolution, so two
workers split by palette would each keep a complete view. That works and stops
at two.

Sharing both maps behind a lock keeps the checks whole at any worker count, and
the lock is held for a dictionary lookup against work measured in seconds. The
partition-by-theme version buys nothing the shared version does not, and caps
the thing being added.

*What this rules out:* letting each worker keep its own maps. A run split four
ways would compare each capture against a quarter of the run, and
`duplicate_problem` would miss exactly the cross-surface collision it exists to
catch -- while still reporting success.

### 3. Threads, not processes

The work is `subprocess` waits and sleeps, so the interpreter lock is released
for nearly all of it. Threads share the store connection and the two maps
directly, where processes would need the maps marshalled back and forth and the
store opened several times.

The store's connection is created with `check_same_thread=False` already, but
that only makes concurrent use *possible*; writes still need serialising, so
store access takes the same lock.

### 4. The worker count is given, never guessed

Defaulting to the processor count would start eight instrumented applications,
each with an audio thread and a repainting window, and the settle poll would
start measuring the machine's load rather than the application's layout. A
capture that fails because the machine was busy is worse than a slow one.

Default stays 1, so nothing changes for anyone who does not ask.

## Risks / Trade-offs

- **Several applications, one machine.** The settle check is a timing
  measurement, and enough parallelism will make it flaky. Mitigated by the
  default of 1 and by the count being explicit, but there is a number above
  which this stops being useful and it will be found by experiment.
- **Failures interleave.** A worker's error output arrives among another's
  progress. The per-capture failure is recorded in the store, which is what the
  review reads, so the console becoming harder to follow costs little.
- **A shared lock is a shared bug.** Everything that touches the store or the
  two maps has to take it, and forgetting is invisible until two workers race.
  Narrow surface -- three call sites -- and worth stating in the code.

## Migration Plan

1. Several displays at once, tested without Xvfb where possible.
2. The pass takes a worker count, still defaulting to 1, with the maps and the
   store behind one lock.
3. A test that a parallel plan and a sequential plan cover the same captures.
4. Measure a real full sweep at 1, 2, and 4 before recommending a number.

## Open Questions

- **How many workers before the settle check gets flaky?** Answerable only by
  running it; the default of 1 means nobody is exposed to the answer until they
  choose to be.
- **Should the hub offer it?** It is the place a full sweep is most often
  started, and also the place where a flaky run is least welcome.

## What running it actually found

Measured on this machine, quick mode, 14 captures, both palettes.

| Workers | Wall clock | Result |
| --- | --- | --- |
| 1 | 29s | 12 captured, 2 failed |
| 2 | 21s | 12 captured, 2 failed — identical |
| 4 | 77s | 4–8 captured, and two applications never answered |

### First reading: the ceiling is the audio device, not the settle check

The open question was how many workers before the settle check gets flaky. That
was the wrong thing to watch. At four workers two applications hang before
capturing anything, on `list-states` — the first command either is sent.

Attached to one and read its stack:

```
AudioInputService::timerCallback         (AudioInputService.cpp:451)
 → scanForDeviceChanges                  (AudioInputService.cpp:569)
   → AudioDeviceManager::initialise
     → ALSAAudioIODevice::open → snd_pcm_prepare
       → libasound_module_pcm_pipewire → pw_thread_loop_wait → pthread_cond_wait
```

The recovery timer reopens the input device when it does not have a usable one,
on the message thread. With another instance already holding the device, that
open never returns — so the message thread never returns, so the thread that
answers the control channel waits on a promise that is never kept.

This is a property of the application, not of this change. It is also a defect
outside the suite: anything that makes an ALSA open block — a device
disappearing, PipeWire restarting — freezes the interface, with no timeout and
no way back. Fixing it means taking device recovery off the message thread,
which is the audio ownership model and belongs in a proposal of its own.

That first reading put the useful count at 2. It was wrong about the cause of
the *ceiling*, and right about the cause of the *hang* — see below.

### A hang is now a failure with a reason

The run used to wait on a pipe forever: no image, no log line, no failed row,
and nothing to read but a stack. `ApplicationDriver.send` now has a deadline,
so an application that stops answering is a recorded failure naming the command
it did not answer.

Found while doing that: `stop` cleared `_process` before calling `send`, so
`send` refused against the very application it was closing. `quit` had never
been sent by anything, ever — every stop waited ten seconds and then killed the
application, which also meant it never left the way a user's would.

## Second reading: the device is contended, so stop overlapping the opens

The hang is real and the stack above is accurate. But gating the one moment
that touches the device — an application opening it — removes it, and that is a
much smaller thing than the worker count.

The gate is held across a start and released before any capturing, because the
device is only contended while it is being acquired. Once an application has it,
the parts a run actually spends its time on — settling for up to six seconds,
warming up a tone, photographing, converting — overlap freely. `restart_before`
surfaces take the same gate, since a restart reopens the device.

With that, on this machine, quick mode, 14 captures:

| Workers | Wall clock |
| --- | --- |
| 1 | 19–29s |
| 4 | **10s** |
| 6 | 11s |

### Work is taken, not dealt

The first version split the plan in advance. That leaves whoever drew the
surfaces carrying a tone still settling while everyone else has stopped, and no
split decided beforehand avoids it, because how long a surface takes is not
known until it is taken. Workers now pull the next group when free.

Two things fell out of that. A worker that cannot start loses nothing — the
groups it never drew are still in the queue. And the worker count is capped by
the number of groups, which the run reports: asking for 20 on an 8-group plan
runs 8 and says so.

### A retry under the gate is worse than no retry

Trying a failed launch again while holding the gate every other worker is
waiting on turned one bad start into seventy seconds of everybody waiting —
slower than not running in parallel at all. And the thing that makes a launch
fail, other instances holding the device, is not something a retry changes. A
worker that cannot start now retires, and its work goes to the others.

The same lesson applied to ending it: `stop` is polite first, five seconds for a
reply and ten for the exit, and being polite to something known not to be
listening spends fifteen seconds proving it twice — on every worker behind it.
`kill` is for that case.

## What this does not fix, and what it exposes

**One application at a time is the real constraint, and it is not new.** With
another Practice Takes open, a *sequential* capture — one worker, the way it has
always run — captures nothing at all: it fails at `list-states` after sixty
seconds. Measured, not inferred. The parallel run under the same conditions
still got 5 of 14.

So capturing has never worked while somebody has the application open. Nobody
noticed because the failure had no voice: the run waited on a pipe. It says so
now, and names the instances it is competing with without touching them.

Fixing it properly means the capture instances not opening the shared input
device at all — a synthetic-input mode, or device recovery off the message
thread. Either is an application change and belongs in its own proposal. It
would also do the thing actually wanted: capture while the application is open.
