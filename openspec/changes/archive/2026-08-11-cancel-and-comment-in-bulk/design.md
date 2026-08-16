## Context

**Nothing holds a run.** `CapturePass.run` walks the plan in a loop and the
loop has no exit but the end of the plan. The hub starts it on a background
thread and keeps a handle to the thread, not to anything that can be told to
stop.

**Killing the application does not stop it.** The pass restarts the application
between surfaces, so the window that is on screen is not the run -- it is the
current capture. Ending a run by hand today means finding the hub process, not
the one with the visible window, which is the opposite of what anybody would try
first.

**A capture is not interruptible mid-way, and should not be.** `capture_one`
asks for a geometry, waits for the window to settle, photographs it, converts it
and writes a row. Stopping between those steps would leave a partial image or a
row with no file, so the check belongs between surfaces.

**Comments are per capture.** `comment(capture_id, body)`. The review can narrow
to a subset and approve it together, so the selection already exists; only
commenting ignores it.

## Goals / Non-Goals

**Goals**

- A run can be stopped from the thing that started it, without knowing anything
  about processes.
- Stopping loses nothing already captured and does not manufacture failures.
- One sentence about one defect is typed once.

**Non-Goals**

- Pause and resume as separate states. Running again *is* resume.
- Interrupting a single capture.
- Cancelling from outside the process that started the run.

## Decisions

### 1. A flag the pass checks between surfaces, not a thread that gets killed

The pass takes something it can ask "should I stop?" at the top of each surface.
Killing the worker thread instead would leave a half-written row, a live
application process, and an Xvfb display with nobody to close it.

Between surfaces is the only safe point, and it is fine: a surface takes at most
a few seconds, so the delay between pressing stop and the run ending is bounded
by one capture.

### 2. Not reached is not failed

The store already distinguishes a capture with a failure from one that was never
attempted -- `already_captured` versus `failed` in the summary. A stopped run
leaves the rest unattempted.

This matters beyond tidiness: the export feeds a release gate, and a run full of
failures that only means "somebody pressed stop" would either block a release or
teach everyone to ignore failures. Both are worse than the run being short.

### 3. Escape, because the mouse may not be available

The reason to have the key at all is the case where the pointer is not usable --
which is exactly what a capture run on the desktop display used to do. Headless
capture removes that, but the key costs nothing and the situation that made it
necessary is the situation where a stop control is most needed.

### 4. Bulk comment writes one row per capture

The alternative is a comment that belongs to a selection, which would need the
selection stored and would raise the question of what a capture shows when it is
read outside that selection. One row each keeps a capture self-contained, which
is what the review reads.

The cost is that editing a bulk comment later edits one of them. Acceptable:
comments are written far more often than edited.

## Risks / Trade-offs

- **A stopped run looks like a short run.** Someone reading the record later
  cannot tell it from one that covered less on purpose. Worth a marker on the
  run if it becomes confusing, but not worth inventing before it does.
- **The delay before stopping.** Bounded by one capture, which is seconds. A
  faster stop would mean interrupting a capture, which is the thing that leaves
  a row without a file.
- **Bulk comment makes it easy to say the same wrong thing many times.** So did
  copy and paste.

## Migration Plan

1. The pass takes a stop signal and checks it between surfaces; tests cover the
   loop without capturing anything.
2. The hub exposes it: a control, an endpoint, and the key.
3. Bulk comment in the review, over the selection that already exists.

## Open Questions

- **Should a stopped run be marked as such in the record?** It would tell a
  later reader why a run is short. It would also be a new field on `run` that
  the export and the gate would have to learn.
