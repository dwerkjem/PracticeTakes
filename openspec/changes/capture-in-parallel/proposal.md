## Why

A default full run is **204 captures**. Each opens a state, applies a palette,
asks for a geometry, and waits up to `SETTLE_SECONDS` (6.0) for the window to
stop moving, plus a warmup for every surface carrying a tone. The run is almost
entirely spent waiting for one application to settle, one window at a time.

That cost is why runs get put off, which is the same failure `--surfaces` and
`--headless` were aimed at from other directions.

Until now there was a good reason to do one thing at a time: the pass drove the
desktop. Two applications resizing themselves on one screen would fight over
focus and photograph each other. Captures now run on a private virtual display
by default, and a second display costs nothing — so the reason is gone.

## What Changes

- **A run may capture on several virtual displays at once.** Each worker gets
  its own display and its own application, and takes a share of the plan.
- **The safety nets keep their reach.** Two checks work by comparing captures
  against each other, and both would develop holes if each worker could only see
  its own slice. They are shared across workers rather than partitioned.
- **Worker count is chosen, not assumed.** Defaulting to every core would put
  eight instrumented applications on one machine and measure the machine rather
  than the build.

## Non-goals

- **No change to what a capture is**, or to the store, or to the review grid.
- **No parallel `attend`.** It is a person answering questions.
- **Not a way to make the settle time shorter.** That wait is what makes a
  capture trustworthy; this overlaps the waits rather than shortening them.

## Capabilities

### Modified Capabilities

- `ui-capture-and-review`: a run may photograph several surfaces at once, and
  what the cross-capture checks must still see when it does.

## Impact

- `tools/scripts/testing_suite/capture.py` — the pass loop, and the two shared
  maps.
- `tools/scripts/testing_suite/display.py` — more than one display at a time.
- `tools/scripts/testing_suite/__main__.py` — the worker count.
- **Not affected:** the store schema, the export, the release gate, and every
  capability outside the testing suite.
