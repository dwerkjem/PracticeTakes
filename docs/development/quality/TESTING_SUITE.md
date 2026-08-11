# The testing suite

Some things can only be checked by a person looking at the screen: whether a
layout is cramped at 800×600, whether a theme applies everywhere, whether the
tuner responds to a real voice. The testing suite is how that gets done
consistently, compared across runs, and recorded.

It is a **separate application** from Practice Takes. It has to verify a build it
is not part of, it adds nothing to the shipping binary, and it keeps working when
the build under test does not.

## Running it

```bash
uv run test-suite
```

That opens the hub: every suite the project has in one place, with a button on
each. Nothing else has to be set up first — if a suite needs a build that does
not exist, the hub builds it, saying so before it starts, because a cold build is
by far the slowest thing here.

The hub has three views:

- **Run** — pick suites, or run everything, or everything of one kind. Progress
  and the live output are on the same page.
- **Review** — the captured surfaces as a grid: tag, comment, and score.
- **Results** — what the suites said about this run, and what was measured.
- **History** — the graphs: pass rate per run over time, and one chart per
  performance metric. Shared through git.

### What it can run

| Suite | Kind | Needs |
|---|---|---|
| C++ unit tests | tests | builds `PracticeTakesTests` |
| Python script tests | tests | — |
| Service tests, service type check | tests | `npm ci` in `src/services` |
| Smoke test | tests | a build with the control channel, and a display |
| Benchmarks | performance | builds `PracticeTakesTests` |
| UI golden images | ui | a build, and a display |
| UI capture | ui | a build with the control channel, and a display |

A suite that needs a display is **skipped with that reason** on a headless
machine rather than failing.

### Without a browser

Every button has a command, so the suite works over a terminal-only session:

```bash
uv run test-suite run --all                     # everything
uv run test-suite run --kind performance        # one kind
uv run test-suite run --suites cpp python       # a selection
uv run test-suite capture                       # unattended captures only
uv run test-suite attend                        # the behavioural questions, live
uv run test-suite review                        # the grid, in a browser
uv run test-suite export                        # the record the release gate reads
uv run test-suite status                        # what is in the store
```

The UI work is still three passes, and the split is the whole point:

- **Capture** drives the application to every surface at every configured
  resolution and photographs each one. Nobody is present.
- **Review** shows all of it in one grid, later, and takes verdicts, tags, and
  comments. The application does not need to be running — or buildable.
- A deliberately short **attended** pass covers the questions a photograph
  cannot answer.

If you would rather build by hand first:

```bash
cmake -S . -B build-tc -DCMAKE_BUILD_TYPE=Debug -DPRACTICE_TAKES_ENABLE_TEST_CONTROL=ON
cmake --build build-tc --target PracticeTakes --parallel
```

**Use `uv run`, not a bare `python3`.** `uv sync` installs into `.venv`, and a
bare `python3` does not look there — so installing Pillow and then running with
`python3` fails with the same "Pillow is not installed" message you were trying
to fix. If you would rather not use `uv run`, call the interpreter in the
virtual environment directly:

```bash
.venv/bin/python3 tools/scripts/testing_suite capture
```

Every command takes `--run` to work on a run other than the newest, and
`--database` to use a store other than the default.

**A run is one build under test**, holding everything anybody learned about it:
captures, verdicts, suite results, and measurements. That is what makes "what
state was this build in" one question rather than five.

**A suite's verdict is its exit status.** Counts scraped out of output are
detail; a suite whose output cannot be parsed still reports pass or fail
correctly, with its counts left unknown rather than reported as zero failures.

## Capture

```bash
uv run test-suite capture --mode full --resolutions default constrained maximised
uv run test-suite capture --themes dark light
uv run test-suite capture --mode quick
uv run test-suite capture --run 12          # resume; already-captured surfaces are skipped
uv run test-suite capture --surfaces tuner-in-tune tuner-bar   # only these
uv run test-suite capture --no-headless     # on the desktop; headless is the default
uv run test-suite capture --scratch         # not in the verification history
```

The three combine, and together they are what a "let me look at this" capture
wants — one surface, on nobody's screen, out of the record:

```bash
uv run test-suite capture --headless --scratch --surfaces tuner-bar \
    --resolutions default --themes dark
```

### Capturing one surface

`--surfaces` names approved states — the same names `list-states` prints and the
review grid shows. Someone who changed the tuner wants the tuner, not a sweep of
every surface at four geometries in two palettes.

Selection composes with `--resolutions` and `--themes` rather than replacing
them: a narrowed run still visits each chosen surface at every configured
resolution and palette. A name no surface offers fails the run before it starts,
because a typo that captures nothing looks exactly like a run where everything
was fine.

The summary prints the directory the images went to, which is usually what you
want when you captured two surfaces to look at a change.

### Capturing without taking over the screen

Capture runs on a private Xvfb screen **by default**. No window appears, nothing
takes focus, and the images are the same — `xwindow_capture` reads a window's
contents from the X server, which does not care whether the screen is attached
to a monitor. Verified rather than assumed: the same static surface captured both ways
produces a byte-identical file, digest for digest.

Install Xvfb with
`bash tools/scripts/build/check-linux-build-dependencies.sh --install`. Without
it, an explicit `--headless` refuses rather than quietly falling back — but the
*default* falls back to the desktop with a notice, because somebody who never
mentioned a display would rather have their capture than a lecture.

`--no-headless` captures on the desktop when that is what you want.

Two limits worth knowing. A headless capture is evidence about layout and state,
not about how the application renders on real hardware — font hinting and
compositing can differ. And `attend` has no headless mode on purpose: that pass
exists to put a live application in front of a person, and a display they cannot
see would defeat it.

### Capturing something you only need once

`--scratch` writes to a temporary store instead of the verification history, so
a capture taken to answer one question does not take a run number in a record
meant for verification passes. The run reports its directory, and the images are
left there rather than deleted on exit — looking at them is the reason the run
happened. The machine's temporary directory clears them eventually.

It refuses to combine with `--database` or `--run`: the first two both choose
where the run lives, and there is nothing in a fresh scratch store to resume.

**Check which binary you are capturing.** `--executable` defaults to
`build-tc/bin/PracticeTakes`, a tree that may be older than the one you just
built. Nothing warns you, and a capture of a stale binary looks exactly like a
capture of a current one.

A capture is a surface, at a resolution, **in a palette**. Themes are a
dimension rather than a property of a surface, for the same reason resolutions
are: every surface exists in both, and folding the palette into the surface list
would double it. The theme is applied *after* the state, because opening a state
rebuilds the workspace and a palette set first would be stale for whatever it
just created.

Themes are the outer loop of a run, so switching palette — one command, instant
— happens once per pass rather than between every resize.

For each surface at each resolution the pass opens the approved state, applies
the palette, asks the application for the geometry, waits for the window to
settle, captures, and converts the result to PNG plus a thumbnail.

**Analysis tools are fed a synthetic tone.** A state can name a frequency, and
the audio service generates it in its own callback instead of the device's
input — the same path a microphone takes, through the same FIFO, at the same
gain. So a capture of the tuner reads a live pitch rather than "Play or sing a
sustained note", and it works on a machine with no microphone at all. It is a
table read and a phase increment in the callback, never `sin` per sample,
because the audio thread may not do unbounded work.

**It is a sung note, not a test tone**, because a pure sine draws a flat line on
the tuner, one bar on the harmonic analyser, and a single stripe on the
spectrogram — a state no real input produces, and one that hides every layout
bug that only appears when a reading moves. So it carries:

| | |
|---|---|
| a slow **drift** of ±45 cents at 0.55 Hz | the tuner's graph draws a wave; the tool smooths over ~0.75 s, so a fast vibrato alone averages back to a line |
| a **vibrato** of ±12 cents at 5 Hz | moves the meter and bar views, which read the current frame |
| second and third **partials** | the analyser shows H1, H2, H3, and the spectrogram three bands |
| a **swell** of ±55% at 0.45 Hz | about 11 dB peak to trough, so a peak, RMS, or loudness meter has something to draw |
| a little **noise** | pitch confidence lands where real input lands rather than at a suspicious 100% |

The peak of the loudest moment lands exactly on the amplitude asked for — the
swell is folded into the scaling — so deepening it never pushes the signal
towards clipping and never lights a clipping indicator that has nothing to
report.

**Nothing is audible.** The generator only fills a buffer pushed into the
analysis FIFOs; the device's output is cleared at the top of the callback and
never written, and this is the only audio callback the application registers.

Surfaces with a tone wait a couple of seconds before being captured, so a tool
drawing a history has one to draw.

The tuner is captured in tune, sharp, and flat, in each of its three views
(graph, bar, meter), and with its advanced settings expanded — six surfaces
where there was one, because a screenshot of the graph says nothing about the
meter.

**The pointer is not parked**, unlike `run-ui-golden.zsh`. An X capture of a
window never includes the cursor, so parking changes only hover state — which
matters when images are compared pixel for pixel, as the golden-image and
launch-timing validation does, and does not matter here, where a person looks at
them.

**A geometry is confirmed, not assumed.** The window is polled until its size
stops changing, and a resolution that produced the same size as another one is
recorded as a *capture failure* rather than as an image. That is not a
hypothetical: an earlier sweep resized the window back to the default and
reported a narrow window as "not narrow" and a fullscreen window as "not
fullscreen". Three identical images labelled as three different sizes is exactly
the failure this workflow exists to catch.

A surface that cannot be reached or captured is **a recorded failure with a
reason**, not a gap. It appears in the grid as a failure card and in the record
as an unreachable surface.

Surfaces whose whole point is their window size — the narrow window, fullscreen
— are captured once, at their own geometry. Resizing them would destroy the
thing under test.

## Review

```bash
uv run test-suite review --run 12 --port 8730
```

Opens a local page bound to loopback. Everything is saved as you go; closing the
browser loses nothing.

### Filtering

A full run is around a hundred captures — 27 surfaces, three resolutions, two
palettes — which is only reviewable if you can ask it a question. Each facet is a
dropdown above the grid, offering every value actually present in the run with
its count:

| Facet | Values |
|---|---|
| `review` | unreviewed · part reviewed · reviewed |
| `verdict` | unanswered · passed · skipped · failed |
| `tag` | whatever you have applied, plus `untagged` |
| `capture` | ok · flagged · failed |
| `theme`, `resolution` | the palettes and sizes the run covered |
| `area` | workspace · settings · audio · feedback · shell |
| `presentation`, `tools`, `tool_count`, `state` | what the surface opens and how |

`verdict` reports the worst thing said about a capture: a failure anywhere makes
it failed, and a skip outranks a pass, because an area nobody examined is not a
pass and a surface with one failure is not a passing surface.

**Clicking a value cycles it**: keep ✓ → exclude ✕ → off. Keeping narrows to
what you named; excluding removes it and leaves everything else — which is what
makes a second pass finishable, since "everything except what I already
approved" is `review: exclude reviewed`. Excluding beats keeping if a value is
somehow both.

Filters combine the way people expect: **within** a facet the values are
alternatives (a capture is dark *or* light), **between** facets they all have to
hold. "Settings, light" is twelve captures; "three tools, constrained" is the
handful that found the clipped workspace; "unreviewed, not settings" is what is
left to do.

What you have chosen shows as chips under the menus — click one to drop it —
with a count of how many captures the filters are hiding.

### Judging from the zoom

Opening an image fills the window, which is the size at which a layout is
actually judgeable — so the verdict belongs there rather than back in the grid.
**Approve** and **Reject** answer every image-answerable question on that
capture and move to the next one in the filtered set, so a pass over twenty
captures is twenty keystrokes. ← and → walk the set without judging. Clicking
the image toggles between fit-to-window and actual size for a detail that needs
it.

### Opening the real thing

Every capture carries **open in app**, which puts a live application into
exactly that state, palette, and window size. A screenshot answers "does this
look right" and stops; the moment it raises a question — is that control really
disabled, does that menu open — the only answer is the application itself, and
this saves remembering which of 27 states produced the image.

One instance at a time: opening another replaces it, and it closes with the hub.
It refuses while a capture is running, because two instances would fight over
the audio device and over which window a capture belongs to.

```bash
uv run test-suite open tuner-docked --theme light --geometry constrained
```

**Approve all shown** answers every image-answerable question `pass` across the
whole filtered set, leaving anything already answered alone. That is the
intended rhythm: narrow to a group you can judge at a glance, look, approve,
move on. The grid says how many are shown and how many the filters are hiding,
so an approval never silently covers captures you did not look at.

| Doing | Gets |
|---|---|
| Click a card | Select it |
| Shift-click | Select a range |
| Ctrl/Cmd-click | Add or remove one |
| Drag across the grid | Band-select |
| Click a tag button | Apply it to the whole selection |
| Shift-click a tag button | Remove it from the whole selection |
| `+ tag` | Add a tag to the vocabulary, immediately usable |
| Click an image | Open it large; Escape returns |
| In zoom: A / F | Approve or reject, then move to the next |
| In zoom: ← / → | Walk the filtered set without closing |
| In zoom: click the image | Toggle fit-to-window and actual size |
| `open in app` | Launch the real application on that surface |
| `comment` | Free text on that one image |
| P / F / S | Score an axis; a fail asks for a reason, which is optional |
| Pass/Fail/Skip selected | Score every selected image at once |

Resolutions of one surface sit beside one another, which is the comparison the
whole workflow exists for — a problem like "too much information in too small an
area" is obvious across three sizes together and invisible when they are minutes
apart.

The tag vocabulary is data, not code: it starts at `broken`, `ugly`,
`illegible`, and grows from the page.

## The attended pass

A question about behaviour is not a question about pixels. "Does the tuner
respond to sound from the microphone?" cannot be answered from a screenshot, and
a workflow that quietly stopped asking it would be verifying that the
application *looks* right while saying nothing about whether it works.

So questions are marked `behavioural` in `surfaces.py`, and those — plus every
question on a surface that carries an instruction, since the tester has to do
something first — are asked by `test-suite attend` against a live application,
one at a time:

```
Enter = pass · f [reason] = fail · s = skip · q = stop
```

They are asked **once per surface**, not once per resolution: the answer does not
vary with window size.

A run whose attended questions are unanswered is **incomplete**, and its record
names them.

## What is asked

Every capture is scored on the same three axes:

1. Does it look correct?
2. Does it look well-presented?
3. Does it work?

Fixed axes are what make runs comparable — you can see that presentation
regressed on the tuner between two versions, which a bespoke question set per
surface could not tell you. Surfaces then add their own questions, recorded
separately so they do not dilute that core.

**Tags and comments never substitute for an axis.** They are additional detail,
which is what keeps a record from the suite comparable against every record the
old terminal harness wrote.

**A failure prompts for a note and does not require one.** Leave it empty and
the failure is still recorded. A note is worth giving — a failure with no detail
says something is wrong without saying what — but nothing blocks on it, and a
run whose failures carry no notes still exports.

## Modes and resolutions

- **`--mode quick`** — the smallest set that answers "does it still work": the
  shell, the tuner, and the settings window.
- **`--mode full`** — every surface. What a release is verified with.

`--resolutions` chooses the window sizes, defaulting to all four:

| Name | Size | Why |
|---|---|---|
| `default` | 1280×800 | what most people open it at |
| `constrained` | 800×600 | below the 900px threshold at which the title bar collapses to the hamburger menu |
| `tiny` | 640×480 | smaller than a window manager would let a user drag it — the size at which a docked tool has to choose what to drop |
| `maximised` | the display | where a layout stretches empty space instead of showing more |

`tiny` exists because most of what a run finds is a layout out of room. It is
the question #113 asks, asked of every tool at once, and it is reachable only
because the application resizes itself through the control channel — the window
advertises a 980px minimum that a window manager honours.

Resizing goes through the control channel, so the application resizes *itself*.
An external resize cannot do this job: the window advertises a 980px minimum
width through `setResizeLimits`, a window manager honours it, and 980 is above
the 900px collapse threshold — so an outside resize could never reach the
collapsed menu.

The mode and the resolutions covered are both recorded with the run, so two runs
that covered different sets are distinguishable rather than silently unequal.

## Nothing is synthesised

The suite never fakes a mouse or a keyboard. It names an approved *state*, and
the application does the rest. That is why a layout change cannot silently
redirect a click and a renamed state fails loudly instead of quietly verifying
the wrong thing. The vocabulary is closed: it lives in
`src/application/testcontrol/ApprovedWindowStates.cpp`, and the whole surface
list is checked against the application's `list-states` before a capture pass
rather than discovering a mismatch halfway through.

Capture itself reuses the X utilities in `tools/scripts/quality/ui-validation/`
— `xwindow_capture`, `window_control`, `pointer_control` — compiled on demand.
There is deliberately no second mechanism for driving or for capturing.

## The store

Runs accumulate in a SQLite database, by default at
`~/.local/share/practice-takes-testing-suite/verification.db`. It holds the
machine, the runs, the captures with their verdicts, tags, and comments, ingested
performance measurements, and automated suite results.

It is **local history, and never the input to a release gate**. It is not in
`build/`, because a clean build would destroy it; and not in git, because a
binary database would conflict on every run and would quietly become the thing
releases depend on. Losing it costs comparison history and never a release.

Images live as files beside it; the database holds each one's path, size, and
digest.

```bash
uv run test-suite prune --keep 5    # drop old images, keep every decision
```

## History and sharing it

A single run answers "is this build all right". The questions worth a database
are the ones only a sequence answers, and the History view draws them:

- **Verification over time** — the share of answered questions that passed, per
  run. Skips count against it: an area nobody examined is not a pass. Runs with
  failures are drawn as larger red points.
- **Performance over time** — one chart per metric, each on its own scale in its
  own unit. Two measures never share an axis, which is why a run's ten
  benchmarks are ten small charts rather than one crowded plot.

Both are filtered to one machine, always. A launch time from another processor
is not a point on this machine's line, and the store refuses to compare across
machines for the same reason.

```bash
uv run test-suite sync        # write this machine's runs into git
uv run test-suite history     # the same numbers, in a terminal
```

`sync` writes one JSON file per run into
`docs/development/quality/run-history/`. One file per run is what makes it
mergeable: two machines committing different runs never conflict. Commit that
directory and pull it elsewhere, and the other machine's runs appear on the
graphs.

**Images are never shared.** A full run is several megabytes of PNG that would
bloat the repository forever and cannot be diffed, so screenshots stay in the
local store and only the numbers travel. The history files carry counts,
measurements, and suite results.

The graphs use Chart.js, vendored under `web/vendor/` so the hub works with no
network — see the note there.

### Machine provenance

Every run records the processor, core count, memory, graphics renderer,
operating system, and display resolution, and is attached to a *machine*
identified by a hash of exactly those. Kernel, distribution, and driver versions
are recorded as attributes but stay **out** of the hash — fold them in and every
system upgrade silently starts a new machine, throwing away the comparison
history the store exists to keep.

A comparison never crosses machines. If this machine has no earlier value for a
metric, the suite says "no baseline on this machine" rather than comparing
against somebody else's hardware.

### Ingesting evidence

The suite runs the benchmark cases itself (the Benchmarks suite) and stores what
they measured. `ingest` is for measurements produced somewhere else — it reads an
export by shape, not by producer:

```bash
uv run test-suite ingest --performance some-measurements.json
uv run test-suite ingest --suite PracticeTakesTests --report build/ctest.log
uv run test-suite ingest --suite services --cases 42 --failures 0
```

Ingest is all-or-nothing: a malformed export stores nothing and says why, because
a half-ingested set makes a run look like it measured less than it did.

## Records and the release gate

`test-suite export` writes two files to `docs/development/quality/manual-runs/`:

- **JSON** — diffable, so two runs can be compared question by question.
- **Markdown** — what a person reads when asking "what did we verify before
  v0.5.7".

Both name the commit, platform, audio device, mode, resolutions covered, and the
machine. Records written by the retired terminal harness sit in the same
directory and remain readable as history.

An unfinished run exports as **INCOMPLETE**, naming what was never answered. A
partial run must never be mistaken for a passing one.

`release.yml` fails before building anything if there is no complete, current,
full-mode run for the code being released. "Current" cannot mean "for this exact
commit" — a record is committed *after* the run it describes — so the gate
matches on code state: the verified commit must be an ancestor of the release
commit, and no *release-affecting* file may differ between them.

Release-affecting paths are an explicit list in
`tools/scripts/release/check_manual_verification.py`. Check the gate locally
before triggering a release:

```bash
python3 tools/scripts/release/check_manual_verification.py
```

Skipping the gate, and waiving a known failure, work exactly as before — see the
`manual-gui-verification` capability in `openspec/specs/` for the rules and
`release.yml` for the inputs.

## Keeping it honest

**When an automated test starts covering a question the suite asks, delete that
question in the same change.** The manual run should get shorter as the suites
grow. A question kept "just in case" after it is automated is a tax on every
release, and an item nobody prunes is how a checklist rots into a ritual.

Surfaces are data, in `tools/scripts/testing_suite/surfaces.py`. Adding one is an
edit to that list.

## Where things live

| | |
|---|---|
| `surfaces.py` | The surface list, and which pass asks what — data |
| `driver.py` | Speaks the control protocol, including geometry |
| `capture.py` | The unattended pass: settle, confirm, capture, convert |
| `images.py` | PPM → PNG and thumbnails (the one Pillow dependency) |
| `store.py` | The SQLite store and its forward-only migrations |
| `machine.py` | Machine provenance and identity |
| `review.py` | What the review decides — outstanding questions, tags, failures |
| `server.py` | The HTTP layer, deliberately thin |
| `web/` | The hub page: fetches and renders, decides nothing |
| `suites.py` | Every runnable suite — id, kind, command, what it needs — data |
| `runner.py` | The background job: builds what is missing, runs, records |
| `attend.py` | The short attended pass |
| `ingest.py` | Measurement exports and automated suite results |
| `export.py` | The record the release gate reads |

Everything except `web/` is standard-library only apart from `images.py`, and is
tested without a display, an application, or Pillow —
`python tools/scripts/run_tests.py`.
