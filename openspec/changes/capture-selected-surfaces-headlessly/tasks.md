## 1. Narrow a run to named surfaces

- [x] 1.1 `surfaces.plan()` takes an optional set of approved-state names and filters `surfaces_for_mode` by it, composing with the existing resolution and palette sets rather than replacing them
- [x] 1.2 An unknown name raises before the run starts, naming what was asked for and what exists (design decision 2)
- [x] 1.3 `--surfaces STATE...` on `capture`, defaulting to every surface the mode covers
- [x] 1.4 Tests: one surface selected, several selected, unknown name rejected, empty selection means everything, selection composes with `--resolutions` and `--themes`

## 2. A display of the run's own

- [x] 2.1 `tools/scripts/testing_suite/display.py` — find a free display number in a private range, start Xvfb, wait for it to accept connections, publish `DISPLAY`, tear down on the way out including when the body raises
- [x] 2.2 Check the socket file *and* a connection to it, so a stale socket from a crashed server is not read as a free number
- [x] 2.3 A missing Xvfb refuses with the install command and does not fall back to the desktop display (design decision 4)
- [x] 2.4 Screen sized above the largest capture geometry, so `maximised` still differs from the ordinary window (design decision 5)
- [x] 2.5 `--headless` on `capture`, entered before anything opens a window and left after the driver stops
- [x] 2.6 Tests that run without Xvfb installed: free-number selection, exhausted range, the refusal, the screen-size floor

## 2b. Finding the window without a window manager (design decision 7)

- [x] 2b.1 `x_window_lookup.h` — one lookup shared by `window_control` and `xwindow_capture`, which carried byte-identical copies
- [x] 2b.2 Fall back to walking the window tree for `_NET_WM_PID` when `_NET_CLIENT_LIST` is absent, taking the largest viewable window of the process
- [x] 2b.3 Confirmed: the first headless run failed with "the window never settled at a size"; with the fallback it captures

## 3. Finding the images

- [x] 3.1 Print the image directory with the run summary, alongside the review command

## 3b. A run you can throw away

- [x] 3b.1 `--scratch` writes to a temporary store under the system temporary directory, so a look-at-this capture does not take a run number in the verification history
- [x] 3b.2 The directory is not cleaned up on exit -- the images have to outlive the process for anyone to look at them
- [x] 3b.3 Refuse `--scratch` with `--database`, and `--scratch` with `--run`: both choose where the run lives, and a scratch store has nothing to resume
- [x] 3b.4 Confirmed: a scratch run left the real store at 12 runs, wrote to /tmp, and numbered itself run 1

## 4. Dependency and documentation

- [x] 4.1 xvfb in `tools/scripts/build/check-linux-build-dependencies.sh`, so `--install` provides it
- [x] 4.2 All three flags in `docs/development/quality/TESTING_SUITE.md`, including that a headless capture is for judging layout and state rather than evidence about rendering on real hardware
- [x] 4.3 Note in the same place that `attend` has no headless mode and why

## 5. Verification

- [x] 5.1 `python tools/scripts/run_tests.py` green, including the new tests
- [x] 5.2 `--surfaces tuner-in-tune` captures one surface and nothing else; confirm against the run's contents rather than the console summary
- [x] 5.3 A misspelled name fails before the run and captures nothing
- [x] 5.4 `--headless` captures the same surface with no window appearing on the desktop, and the image is equivalent to the desktop capture
- [x] 5.5 `--headless` on a machine without Xvfb refuses and names the install command
- [x] 5.6 Confirm `maximised` under `--headless` produces a window wider than `normal`, which is what the screen-size floor exists for
