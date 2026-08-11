## Context

**A build only happens on the way to something else.** `Job.start` takes suite
ids, opens a run, works out which targets those suites need, builds them, and
runs them. There is no entry point that stops after the build, and no route that
asks for one.

**Build state is already computed, and already sent.** `build_state` reports
`present`, `stale`, and a reason per target, and `overview()` puts them on every
page load. The page turns them into two sentences. Everything a colour needs is
already arriving.

**The hub is a long-lived process serving files it re-reads.** `web_assets()`
maps the page's files once, but `_send_file` reads each one from disk per
request, while `import review` happened at startup and never happens again. So
the page can move forward while the code behind it does not — and there is no
version, stamp, or handshake between them that would notice.

**Restarting is not something the hub can be asked for.** Ctrl-C in the terminal
it was started from, then retype the command. The terminal is usually behind the
browser, and the command usually had options.

## Goals / Non-Goals

**Goals**

- Ask for a build, get a build, and nothing else.
- Whether there is something to run is answered before reading anything.
- The hub reports being out of date, and fixes it where it reports it.

**Non-Goals**

- Restarting on its own when the source changes.
- Building on its own when a target goes stale.
- Any second process, supervisor, or wrapper script.

## Decisions

### 1. A build is a job, but not a run

`Job.start_build` reuses the job's state machine — one at a time, a progress
percentage, a log, the stop flag — and skips `store.start_run` entirely.

The alternative was a run with no suites in it, which would have been less code.
It was rejected because the run history is the release gate's input and a run is
a record of what was verified. A build verifies nothing. A row with no captures,
no verdicts, and no results under it would be indistinguishable in the history
from a run that failed to do anything, and the two mean opposite things.

### 2. Per target, not one button for both

`PracticeTakes` and `PracticeTakesTests` are separate build trees with different
CMake options, and they go stale independently. One button would have to either
build both — several minutes of work nobody asked for — or pick one and leave
the label lying about which.

### 3. Colour is the state, and the reason stays in the title

Red for absent, green for present. Not a third colour for stale: "built" and
"not built" is the question the button answers, and a traffic light whose amber
means "it will work but might be wrong" is read as a warning to click through.
Staleness keeps its own notice, which now has a button in it.

### 4. The notice does the thing it describes

The stale notice used to end "Tick `rebuild` to be sure", naming a checkbox
somewhere else that changes what a later action does. It now carries a button
that rebuilds that target. Instructions that could have been a button are how
the checkbox came to be ignored.

### 5. `execv`, not spawn-and-exit

The hub holds the port. Starting a second process and exiting leaves two hubs
briefly contending for it, and the loser dies with no page left to say why.
`os.execv` replaces the image in place: the socket closes with the old code
because Python marks its file descriptors close-on-exec, and the new process
binds it as it starts. The reply goes out first, on a short timer, because a
restart the page never hears about looks like a button that did nothing.

### 6. The page waits for a different process, not for any process

A restart keeps the pid and often has the port back inside a second, so "wait
for it to go down, then reload" is a race — and the losing side reloads into the
old code, which is the exact fault being escaped. Measured while building this:
the port answered 200 one second after the restart was accepted, with no
observable gap.

So the overview carries a `boot` id, stamped once per process, and the page
reloads when a *different* one answers. This is the only reliable signal
available; everything else about the process is preserved across `execv` by
design.

### 7. Refused while something is running

`execv` would take a capture pass down with it mid-surface, leaving a run that
stops for no recorded reason. The restart is refused with what is running rather
than queued behind it, because a restart that happens twenty minutes later is
not what anybody pressing it meant.

Verified in passing: the refusal fired against a real build that was underway,
rather than in a test that arranged one.

### 8. Modules only, for the staleness check

`freshness.suite_staleness_warning` compares Python and ignores `web/`. The page
assets are re-read per request and so cannot fall out of step; warning about
them would be a warning that appears on a working hub, which is how a warning
stops being read. Same reasoning as the stale-binary check that module already
holds — the one that exists because a capture of an old build reported success
twice.

## Risks / Trade-offs

**The restart inherits the command line, not the environment it was started
in.** `execv` keeps the environment, so this holds for anything set in it; but a
hub started from a shell function or an alias restarts as the underlying
command. The two adjustments made — inserting `hub` when no subcommand was
given, and adding `--no-browser` — are exactly the two places where re-running
what was typed is not what was meant.

**A failed exec leaves the old hub running.** Deliberately: it keeps serving,
stale but working, and says why on the terminal. The page reports that it never
came back rather than claiming success.

**The build button can be pressed while the tree is in a state cmake cannot
configure.** It fails the way every other job fails — a failed state, the log,
and no run to explain away.
