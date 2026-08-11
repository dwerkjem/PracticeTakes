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
