# Tasks

## 1. Building on its own

- [x] 1.1 `Job.start_build`, reusing the job's state machine and skipping `start_run` entirely
- [x] 1.2 Honour a stop between targets — the second of two builds is another several minutes
- [x] 1.3 `/api/build`, refusing an unknown target by name and refusing to start beside a running job
- [x] 1.4 Tests: what gets built, that no run row appears, that no suite runs, and that a failing build reports rather than raises

## 2. The build state as a colour

- [x] 2.1 One button per target beside the run buttons, red when absent and green when present
- [x] 2.2 A button for every target at once, red unless all of them are there
- [x] 2.3 Disabled while a job is running, rather than queueing a second one
- [x] 2.4 The missing and stale notices carry the build they describe, replacing "tick rebuild and run something"

## 3. Restarting from the page

- [x] 3.1 `restart.restart_command` — keep the options, insert `hub` when no subcommand was given, add `--no-browser`
- [x] 3.2 `os.execv` on a short timer, so the reply reaches the page before the process is replaced
- [x] 3.3 `/api/restart-hub`, refused while a run or build is under way
- [x] 3.4 A boot id per process, so the page reloads for a *different* hub rather than for any answer
- [x] 3.5 The control lives in the stale banner and nowhere else
- [x] 3.6 Tests for the command; the exec itself is verified by doing it

## 4. A hub older than the code it serves

- [x] 4.1 Stamp the suite's source time at import, and compare it on every request
- [x] 4.2 Warn on every view, not only where a run is started -- the mismatch breaks controls everywhere
- [x] 4.3 Compare modules only; the page assets are re-read per request and cannot fall out of step
- [x] 4.4 Tests, including the two ways of not crying wolf: an unchanged hub and a changed asset

## 5. Verification

- [x] 5.1 `python tools/scripts/run_tests.py` green
- [x] 5.2 Edit a module under a running hub, confirm the banner appears, and restart from it
- [x] 5.3 Confirm the boot id changes across a restart, and that the hub keeps its port and options
- [x] 5.4 Confirm a restart is refused while a build is running — done against a real build, not an arranged one
- [x] 5.5 Build one target from the button and confirm it lands, with no run behind it
- [x] 5.6 Confirm the banner and its restart button are absent on a current hub, and that a stylesheet rule cannot resurrect them
