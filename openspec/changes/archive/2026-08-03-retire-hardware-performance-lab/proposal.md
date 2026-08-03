## Why

The in-application Performance Lab was built to make hardware measurements
comparable: run a scenario repeatedly, capture provenance, aggregate trials,
compare strategies, export a record. The testing suite now does that job from
outside the application — it runs the `[.benchmark]` Catch2 cases, stores every
measurement against a machine identified by its stable hardware, refuses to
compare across machines, and graphs each metric over time.

That leaves the Lab as a second, heavier answer to a question already answered.
It is a GUI inside the product, behind its own configure flag, carrying its own
runner, record store, codec, comparison, aggregation, telemetry, scenario, and
strategy registry — roughly 1,700 lines and 25 translation units that exist to
produce numbers the suite now produces without shipping anything.

## What Changes

- **BREAKING**: remove the Performance Lab window, its controller, and the
  `PRACTICE_TAKES_ENABLE_PERFORMANCE_LAB` configure flag. The application no
  longer contains a benchmarking mode.
- Remove the benchmarking engine it existed to drive: runner, record store and
  codec, comparability and comparison, trial aggregation, telemetry collection,
  strategy registry, and the sustained-analysis scenario.
- Keep `ApplicationLaunchTimer`: the application measures its own launch in
  normal operation, and the golden-image validation reads that.
- Keep `test-suite ingest --performance`. It reads a measurement export by
  shape, not by producer, so it remains the way any external measurement enters
  the store.
- Remove `tools/scripts/quality/run-performance-lab.sh` and the hardware
  acceptance procedure written around the Lab.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `hardware-performance-lab`: removed in full. Every requirement it defines is
  about an in-application benchmarking GUI that no longer exists; what replaced
  it is specified by `test-suite-hub` and `verification-run-store`.

## Impact

- **Deleted**: `src/features/performance/` except the launch timer,
  `src/application/shell/ui/performance/`, their tests, and the run script.
- **Changed**: `CMakeLists.txt` loses the option and its source entries;
  `MainComponent` loses the menu item and the window it owned.
- **Unaffected**: benchmark coverage. The `[.benchmark]` cases stay where they
  are, and the suite runs them as its performance suite.
- **Docs**: the hardware acceptance procedure goes; the agent guide's Performance
  Lab commands go.
