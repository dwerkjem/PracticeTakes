## Context

Manual verification today is `tools/scripts/manual_gui/`: a Textual TUI (`app.py`)
over a deliberately UI-free core — `surfaces.py` (what to look at, as data),
`driver.py` (the test-control channel to the application), `session.py` (what
happens next), and `record.py` (the run record the release gate reads). That
split is the part worth keeping; the terminal renderer is the part that has to
go, because a contact sheet of images is not something a terminal can show.

Two capture mechanisms already exist and must not become three. The golden-image
validation script `tools/scripts/quality/ui-validation/run-ui-golden.zsh` builds
and drives three small X utilities: `xwindow_capture` (writes a P6 PPM of a
window, given a PID, with an optional title match and top crop),
`window_control` (activate / maximize / restore / resize by PID), and
`pointer_control` (park the pointer so hover state does not pollute a capture).
The application under test exposes approved states over a stdin/stdout control
channel when built with `-DPRACTICE_TAKES_ENABLE_TEST_CONTROL=ON`.

Constraints that shape everything below: the repository root is closed to new
entries, so this lives under `tools/`; `pyproject.toml` keeps the commit gate
standard-library-only and scopes every real dependency to a group; the release
gate `tools/scripts/release/check_manual_verification.py` reads JSON records
from `docs/development/quality/manual-runs/` and must keep working against both
new and historical records.

## Goals / Non-Goals

**Goals:**

- Capture runs unattended; review is a separate, later, attended pass.
- One durable store where a verdict, a measurement, and a test result from
  different runs on the same machine can be compared.
- A review surface that shows the same surface at several resolutions side by
  side, and lets a reviewer tag many images at once and comment on any one.
- The release gate and every historical run record keep working untouched.
- Reuse the existing capture utilities and control channel exactly as they are.

**Non-Goals:**

- Measuring performance. The Performance Lab (`hardware-performance-lab`)
  measures; this suite ingests its export, stamps it with machine provenance,
  and compares it across runs.
- Running the automated suites. It ingests their results; `ctest`, `vitest`, and
  `run_tests.py` stay the runners.
- Replacing golden-image validation. `run-ui-golden.zsh` answers a different
  question (did the pixels change) and stays.
- CI participation. Verification still needs a display, an audio device, and a
  human, exactly as the current spec says.
- Cross-platform capture. X11/Linux only, as today.

## Decisions

### Python, with the store in SQLite and the review UI in a browser

The tooling language is already Python 3.13, and the core being carried over is
Python. SQLite comes with it — no server, one file, real queries for the
across-run comparisons that are the whole point of having a database.

The review UI is a **local web page served by a small `http.ThreadingHTTPServer`**
with a vanilla-JS front end, launched with `test-suite review` and opened in the
default browser. A browser gives scrolling, image decoding, zoom, and rubber-band
multi-select for free; a grid of forty images is exactly what it is good at.

Alternatives considered:

- *Textual (keep the TUI).* Rejected: the deliverable is a contact sheet.
- *A JUCE desktop window.* Rejected: it drags the application's toolchain into
  its own test harness, and the suite has to work when that build is broken.
- *An in-app Performance-Lab-style panel.* Rejected for the same reason, plus it
  would put a review surface in the shipping binary.
- *FastAPI/Flask + a front-end build.* Rejected: the API is roughly eight JSON
  endpoints and a static-file route. A framework and a bundler would be more
  moving parts than the thing they serve, and the front end is meant to be the
  thin layer.

### The database is machine-local; the git-tracked record stays the contract

The SQLite file lives under the XDG data directory
(`~/.local/share/practice-takes-testing-suite/verification.db` by default,
`--database` overrides), **not** in `build/` and **not** in git.

- `build/` is deleted by `--clean`, and history that a clean build destroys is
  not history.
- Committing a binary database would conflict on every concurrent run and make
  the store — rather than the exported record — the thing releases depend on.

So the store accumulates locally, and each completed run still **exports** the
JSON plus Markdown record into `docs/development/quality/manual-runs/`. The gate
keeps reading files; the database is additive and never load-bearing for a
release. A lost or deleted database costs history, never a release.

### Images live on disk next to the database, not as blobs

A run is roughly fifteen surfaces × three geometries; PPM captures are several
megabytes each. Captures are converted to PNG, thumbnailed, and stored as files
under the data directory with the database holding the path, dimensions, and a
SHA-256. Blobs in SQLite would bloat every backup and buy nothing — the server
can serve a file directly.

The suite writes PNG and discards the intermediate PPM. Conversion and
thumbnailing use **Pillow**, declared in a `testing-suite` dependency group so
the standard-library-only commit gate is unaffected. Hand-rolling a PNG encoder
on `zlib` is possible, but downscaling a million pixels per thumbnail in pure
Python is where that stops being reasonable.

### Machine identity is a hash of stable hardware facts only

A measurement is comparable only against measurements from the same machine, so
each run records a machine identified by a hash over CPU model, core count,
total RAM, GPU renderer string, OS name, and display resolution. Kernel version,
distribution version, and driver versions are **stored as attributes but kept
out of the hash**: an upgrade to any of them should annotate the timeline, not
silently start a new machine and lose comparability.

### Forward-only numbered migrations

A `schema_version` table plus numbered migration functions applied on open. The
store outlives any one version of the suite; a database from three months ago
must open. No down-migrations: the recovery path for a bad migration is a
restored copy of the file, which is one `cp` because it is one file.

The tables, in outline:

| Table | Holds |
|---|---|
| `machine` | provenance hash, CPU, RAM, GPU, OS, display, first/last seen |
| `run` | machine, commit, build configuration, mode, geometry sweep, started/finished, complete flag |
| `capture` | run, surface, geometry, image path, dimensions, digest, capture failure reason |
| `axis_verdict` | capture, axis (correct/well-presented/works), verdict, note |
| `tag` | tag vocabulary — name, description |
| `capture_tag` | capture, tag, applied-at |
| `comment` | capture, free text, written-at |
| `measurement` | run, metric name, value, unit, scenario, source |
| `test_result` | run, suite, case count, failures, duration, raw summary |

### Capture and review are separate commands over one store

`test-suite capture` drives the application and writes captures; `test-suite
review` serves the grid over what capture wrote; `test-suite export` writes the
record for the gate; `test-suite ingest` folds in a Performance Lab export or an
automated suite result. Splitting them is what makes "capture unattended, review
later" true rather than aspirational, and it means a review pass never needs the
application under test to be running or even buildable.

### The three axes stay the comparable core; tags are additive

Every capture is scored on correct / well-presented / works, exactly as today,
and those are what export into the record's answers. Tags (`broken`, `ugly`,
`illegible`, plus anything a reviewer adds — vocabulary is a table, not a
constant) and comments ride along in the export as extras. A tag never
substitutes for an axis, so runs stay comparable across the change.

### Carried-over core, thin new UI

`surfaces.py`, `driver.py`, and `record.py` move into `tools/scripts/testing_suite/`
essentially unchanged. `session.py`'s sequencing splits: the plan (which surface
at which geometry) drives the capture pass; the answering half becomes store
operations the review server calls. `app.py` and its typed-line grammar are
deleted with the TUI. Everything except the HTTP layer and the browser code is
importable without a display and covered by `python tools/scripts/run_tests.py`.

## Risks / Trade-offs

- **Unattended capture is where flakiness will live** — a resize the window
  manager has not applied yet produces a capture of the wrong size, silently.
  → After each geometry change, re-read the window geometry and require it to
  match before capturing; settle for a fixed interval first; on mismatch or
  timeout, record a capture failure with the reason rather than an image.
- **A surface that fails to capture could look like an absence** → capture
  failures are rows in `capture` with a reason and no image, render in the grid
  as failure cards, and export as failed answers — the same rule the current
  spec sets for unreachable surfaces.
- **Pillow is a new dependency** → scoped to a dependency group, tools-only,
  never touched by the commit gate or by CI's standard-library scripts.
- **Browser code is untested** → all decisions (plan, verdicts, tagging,
  export, comparison queries) live server-side in tested modules; the front end
  fetches and renders.
- **The database can drift from the exported records** → the export is generated
  from the store and the gate reads only the export, so drift is visible as a
  diff rather than as disagreement between two sources of truth.
- **Image accumulation** — tens of megabytes per run → PNG rather than PPM, plus
  a documented prune command that drops images for runs older than a keep count
  while leaving their rows and verdicts intact.
- **Losing the TUI loses a workflow people know** → the change ships the
  replacement and the workflow document together, and old records stay readable,
  so nothing about past runs becomes unreadable.

## Migration Plan

1. Build `tools/scripts/testing_suite/` alongside the existing harness, porting
   the core modules and their tests.
2. Run one verification through the new suite and confirm its exported record is
   accepted by `check_manual_verification.py` next to a historical TUI record.
3. Delete `app.py`, the `ui-test` console script, and the `manual-gui`
   dependency group in the same change.
4. Update `openspec/specs/manual-gui-verification/spec.md` and the quality
   documentation; leave `docs/development/quality/manual-runs/` untouched.

Rollback is `git revert`: the store is machine-local and additive, and no
released artifact depends on it.

## Open Questions

- Which resolutions the default sweep should cover beyond today's constrained
  (800×600), default, and maximised — a HiDPI entry would catch a class of
  problem the current three cannot, but nothing in the capture path is
  scale-aware yet.
- Whether CI should ingest automated suite results into a shared store later, or
  whether the store stays strictly local to a developer machine.
- Wayland. Everything here is X11, as the existing utilities are; a Wayland
  capture path is a separate change.
