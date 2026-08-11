## Why

Capturing is all-or-nothing and it takes over the screen.

`test-suite capture` photographs every surface the mode covers. There is no way
to ask for one. Someone who changed the tuner and wants to see the tuner runs
the whole sweep — every surface, at four geometries, in two palettes — and waits
through windows opening, resizing four times each, and switching palette, on the
display they are trying to work on.

The two consequences compound. The cost of looking is high enough that people
stop looking, which is the failure this harness exists to prevent; and the only
way to see one surface quickly is to open the application by hand and photograph
it, which is exactly the manual step the capture pass replaced.

The review pass already solved half of this: it can be narrowed to a subset
(`ui-capture-and-review`, "A review can be narrowed to a subset and approved
together"). Capture cannot.

## What Changes

- **A capture run can be narrowed to named surfaces.** `--surfaces tuner-in-tune
  tuner-bar` captures those and nothing else. An unknown name is an error rather
  than an empty run, because a typo that silently captures nothing is
  indistinguishable from a surface that is fine.
- **A capture run can use a display of its own.** `--headless` runs the pass on
  a private Xvfb screen. No window appears, nothing takes focus, and the
  captured pixels are the same — `xwindow_capture` reads a window's contents
  from the X server, which does not care whether the screen has a monitor.
- **A capture run can be thrown away.** `--scratch` writes to a temporary store
  instead of the verification history, so a capture taken to answer one question
  does not become run 47 in a record meant for verification passes. The images
  outlive the process — looking at them is why the run happened — and the
  machine's temporary directory clears them eventually.
- **The run prints where the images went.** Someone who captured two surfaces to
  look at a change wants the files, not a browser grid.
- **Xvfb becomes an installable development dependency** through the existing
  `check-linux-build-dependencies.sh --install`, and its absence is reported
  with the command that fixes it rather than as a connection failure from a tool
  three layers down.

## Non-goals

- **No change to what a capture is.** Same control channel, same
  `xwindow_capture`, same store, same review grid. This changes which surfaces a
  run visits and which screen it visits them on.
- **No headless mode for `attend`.** That pass exists to put a live application
  in front of a person; a display they cannot see would defeat it.
- **Not a substitute for looking at real hardware.** Font hinting and
  compositing can differ under Xvfb. This is for the pass that only produces
  images.
- **No CI capture leg.** Making this runnable unattended is a precondition for
  one, not the thing itself.

## Capabilities

### Modified Capabilities

- `ui-capture-and-review`: two additions to the capture pass — selecting which
  surfaces a run covers, and running it on a display of its own.

## Impact

- `tools/scripts/testing_suite/surfaces.py` — `plan()` takes an optional set of
  states.
- `tools/scripts/testing_suite/display.py` — new; the Xvfb lifecycle.
- `tools/scripts/testing_suite/__main__.py` — two options on `capture`.
- `tools/scripts/build/check-linux-build-dependencies.sh` — xvfb.
- `docs/development/quality/TESTING_SUITE.md` — both flags.
- **Not affected:** the review grid, the store schema, the export, the surface
  list itself, and every capability outside the testing suite. The application
  is untouched.
