## 1. Package skeleton and dependencies

- [x] 1.1 Create `tools/scripts/testing_suite/` with `__init__.py` and a
  `__main__.py` exposing subcommands `capture`, `attend`, `review`, `ingest`,
  `export`, and `prune`
- [x] 1.2 Add a `testing-suite` dependency group to `pyproject.toml` holding the
  image dependency, make it a default group, and fix `[tool.setuptools]
  package-dir` to point at `tools/scripts` (it currently names a `scripts`
  directory that does not exist at the root)
- [x] 1.3 Add the `test-suite` console script; leave `ui-test` in place until
  section 9 removes it
- [x] 1.4 Add `src/tests`-style mirrored test files under
  `tools/scripts/testing_suite/` following the `test_*.py` convention
  `tools/scripts/run_tests.py` discovers, and confirm discovery finds them

## 2. Store: schema and migrations

- [x] 2.1 Implement `store.py`: open-or-create a SQLite database at the XDG data
  path, `--database` override, `schema_version` table, forward-only numbered
  migrations applied on open
- [x] 2.2 Refuse to open a store whose schema version is newer than the running
  suite, with a message saying so
- [x] 2.3 Create the tables: `machine`, `run`, `capture`, `axis_verdict`, `tag`,
  `capture_tag`, `comment`, `measurement`, `test_result`
- [x] 2.4 Seed the tag vocabulary with `broken`, `ugly`, `illegible`
- [x] 2.5 Test: a store created by migration 1 opens and migrates under the
  current schema with its rows intact; a newer-version store is refused

## 3. Store: machine provenance

- [x] 3.1 Implement `machine.py`: collect processor model, core count, total
  memory, graphics renderer, operating system, and display resolution
- [x] 3.2 Derive the machine identity hash from those stable facts only; record
  kernel, distribution, and driver versions as run attributes outside the hash
- [x] 3.3 Attach each run to its machine, creating the machine row on first sight
- [x] 3.4 Test: identity is stable across a changed kernel version and changes
  when processor or memory changes

## 4. Capture pass

- [x] 4.1 Move `surfaces.py`, `driver.py`, and `record.py` into
  `tools/scripts/testing_suite/`, with their tests, unchanged in behaviour
- [x] 4.2 Split `session.py`: keep the plan (surface × geometry) as the capture
  pass's input; delete the answer-then-advance loop and the typed-line grammar
- [x] 4.3 Implement `capture.py`: build the X utilities if absent, park the
  pointer, launch the application under test with the control channel, walk the
  plan, and write one image per surface per resolution
- [x] 4.4 Re-read window geometry after each resize and require it to match the
  request within the settling time before capturing
- [x] 4.5 Record a capture failure with its reason — unopenable state, geometry
  mismatch, capture utility failure, application exit — instead of an image, and
  continue to the next surface
- [x] 4.6 Honour `fixed_geometry` surfaces: capture once at their own geometry
- [x] 4.7 Convert each PPM to PNG, generate a thumbnail, store both with
  dimensions and SHA-256, discard the PPM
- [x] 4.8 Make an interrupted capture pass resumable: already-captured surfaces
  are not recaptured
- [x] 4.9 Test: the plan covers every surface at every resolution and leaves
  fixed-geometry surfaces alone; a geometry mismatch produces a failure row; a
  resumed pass skips what exists

## 5. Review server and grid UI

- [x] 5.1 Implement `server.py` on `ThreadingHTTPServer`: JSON endpoints for
  runs, captures, verdicts, tags, comments, and the tag vocabulary, plus static
  file and image routes; bind to loopback only
- [x] 5.2 Implement the grid page: all captures for a run in one scrollable
  grid, grouped by surface, resolutions side by side, thumbnails from the store
- [x] 5.3 Render a failed capture as a failure card carrying its reason, not as
  a missing tile; render a missing or digest-mismatched image as missing and a
  pruned one as pruned
- [x] 5.4 Zoom: open an image at captured size and return to the grid with
  scroll position and selection intact
- [x] 5.5 Multi-select (click, shift-click, drag) and apply or remove a tag
  across the whole selection in one action
- [x] 5.6 Add a tag to the vocabulary mid-review and make it immediately
  applicable
- [x] 5.7 Per-image free-text comment, persisted on entry
- [x] 5.8 Score the three axes per capture, require a note on a failure, and
  persist each answer as it is given
- [x] 5.9 Show a run's unscored captures so an unfinished review is visible
- [x] 5.10 Test the server's handlers against the store directly — tagging a
  multi-select, adding a tag, commenting, scoring, note-required-on-failure —
  without a browser

## 6. Attended pass

- [x] 6.1 Mark surfaces whose questions are behavioural (live input, control
  behaviour, restart persistence) in `surfaces.py`
- [x] 6.2 Implement `attend.py`: drive only those surfaces with the application
  live and collect their answers into the same run
- [x] 6.3 Record unanswered attended questions as unanswered, and make a run with
  any of them incomplete
- [x] 6.4 Test: the attended plan is the behavioural subset; an unanswered
  attended question makes the run incomplete

## 7. Ingest: measurements and automated results

- [x] 7.1 Implement `ingest.py` for the Performance Lab export: store metric,
  value, unit, and scenario against the run; fail atomically on a malformed
  export
- [x] 7.2 Ingest automated suite results — `ctest`, `npm run test`,
  `run_tests.py` — as suite name, case count, failure count, and duration
- [x] 7.3 Implement the same-machine comparison query, reporting "no baseline on
  this machine" rather than comparing across machines
- [x] 7.4 Test: ingest round-trips a sample export; a malformed export leaves no
  partial measurements; comparison never spans machines

## 8. Export and the release gate

- [x] 8.1 Implement `export.py`: generate the JSON and Markdown record from the
  stored run into `docs/development/quality/manual-runs/`, with the fields the
  gate reads today
- [x] 8.2 Export tags and comments as additional detail beside the answers, never
  in place of an axis
- [x] 8.3 Mark a run incomplete when captures are unscored or attended questions
  unanswered, and name what was missing
- [x] 8.4 Refuse to export while a failed axis has no note
- [x] 8.5 Run a real verification end to end and confirm
  `tools/scripts/release/check_manual_verification.py` accepts the exported
  record and still reads the historical TUI records in the same directory
- [x] 8.6 Test: export of a stored run produces a record the gate's loader parses

## 9. Retire the TUI

- [x] 9.1 Delete `tools/scripts/manual_gui/` and its tests
- [x] 9.2 Remove the `ui-test` console script and the `manual-gui` dependency
  group from `pyproject.toml`
- [x] 9.3 Update every reference to `uv run ui-test` in `docs/` and
  `CONTRIBUTING.md` to the new commands
- [x] 9.4 Confirm `python tools/scripts/run_tests.py` still discovers tests and
  passes

## 10. The hub

- [x] 10.1 Add `suites.py`: every runnable suite as data — id, kind, command,
  build dependencies, output parser, whether it needs a display
- [x] 10.2 Generalise the background job in `runner.py` to run any selection of
  suites, building missing targets first and once per target
- [x] 10.3 Build with a sanitised `PATH`, so Nix's loader cannot shadow the
  system one and break `juceaide` halfway through
- [x] 10.4 Report progress, streamed output, and a per-suite verdict taken from
  the exit status rather than from parsed counts
- [x] 10.5 Record every suite result and every benchmark measurement against the run
- [x] 10.6 Skip display-needing suites with a reason when there is no display
- [x] 10.7 Serve the hub: suite list, build state, run/selection endpoints, job
  status, and the review grid as one page with three views
- [x] 10.8 Make a bare `test-suite` open the hub, and add `test-suite run` for a
  terminal-only session
- [x] 10.9 Test the registry, the parsers, the job, and the page's wiring

## 11. History and sharing

- [x] 11.1 Add `history.py`: pass rate and measurements per run, merged from the
  local store and the version-controlled directory, keyed so a run known from
  both is counted once
- [x] 11.2 `test-suite sync` — one JSON file per run, so two machines never
  conflict; no image, thumbnail, or path leaves the machine
- [x] 11.3 A History view: pass rate over time, one chart per metric, stat tiles,
  machine picker, and a table view of the same numbers
- [x] 11.4 Vendor Chart.js under `web/vendor/` so the graphs work with no network
- [x] 11.5 Never merge machines in a series, and say so on the page
- [x] 11.6 `test-suite history` for a terminal-only session
- [x] 11.7 Test the pass-rate rule, the merge, the sync, and what is shared

## 12. Prune and housekeeping

- [x] 12.1 Implement `prune`: delete image files for runs beyond the keep count
  while keeping verdicts, tags, comments, measurements, and results
- [x] 12.2 Ignore the data directory in `.gitignore` if any part of it can land
  inside the repository

## 13. Documentation and specs

- [x] 13.1 Write the workflow document under `docs/development/quality/` —
  capture, attend, review, ingest, export, prune — and index it in
  `docs/development/README.md`
- [x] 13.2 Update `docs/development/agents/AGENT_GUIDE.md` where it describes the
  manual verification commands
- [x] 13.3 Run `openspec validate testing-suite-application --strict`
- [x] 13.4 Sync the delta into `openspec/specs/manual-gui-verification/spec.md`
  and add the new capability specs
- [ ] 13.5 Close issue #143 with a link to the run record the new suite produced
