# Performance Hardware Pilot And Acceptance

This protocol produces the physical-hardware evidence required before the Performance Lab can be
considered for normal builds. CI results validate implementation contracts but are not accepted as
hardware performance evidence.

## Reference Matrix

Run the complete protocol on each row. Record exact detected values from the exported report; do
not substitute marketing names or inferred values.

| System | OS | CPU and memory | Audio device and backend | Sample rates | Buffer sizes | Status |
| --- | --- | --- | --- | --- | --- | --- |
| Linux development workstation | Linux | 11th Gen Intel Core i7-11700KF, 33482080256 bytes | Current audio device and backend | 48 kHz | 256 | Local pilot accepted |
| Windows reference system | Current supported Windows release | Record from report | WASAPI production device | 44.1 and 48 kHz | 128, 256, 512 | Pending |
| macOS reference system | Current supported macOS release | Record from report | CoreAudio production device | 44.1 and 48 kHz | 128, 256, 512 | Pending |

If a platform is not currently supported for release, mark its row `Not applicable` with an owner
and rationale rather than silently omitting it.

## Controlled Conditions

1. Use an AC power source and the normal release power profile. Disable battery-saving and boost
   overrides that are not part of normal use.
2. Reboot, wait five minutes after login, close unrelated applications, pause scheduled updates and
   synchronization, and disconnect unused audio devices.
3. Record ambient process CPU before every run. Restart the protocol if sustained unrelated load
   exceeds 5% or if an update, backup, indexing job, or notification interrupts a trial.
4. Use the same executable, commit, build type, device, backend, sample rate, buffer size, fixture,
   and instrumentation version for each comparison pair.
5. Allow a two-minute idle stabilization period before the first run and 60 seconds between runs.
   If temperature or clock data is available without elevated privileges, record it before and
   after each run. Otherwise record it as unknown.

## Representative Workload

Use the generated `generated-harmonic-fixture-v1` with the `sustained-audio-analysis` scenario.
Run one warm-up trial followed by five measured trials initially. The scenario must use production
analysis paths and must complete its correctness assertion before a comparison is accepted.

## Pilot Ordering And Trial Count

Use interleaved order to reduce drift: baseline A, parameterized strategy B, B, A, A, B. Alternate
which strategy runs first on the next machine. Never discard an unfavorable completed trial.

For each strategy, inspect median, tail percentile, variability, ambient CPU, deadline misses,
dropouts, underruns, and any available thermal or frequency evidence. Five measured trials remain
the default only when continuous-metric variability is at most 5% and no ordering or thermal trend
is visible. Otherwise increase the default to ten and repeat; unresolved instability blocks
acceptance.

## Instrumentation Calibration

For every matrix row, run otherwise identical scenarios with real-time instrumentation disabled
and enabled. Record both exported run IDs and calculate absolute and relative change for scenario
latency, CPU, callback timing, and event counts. Set the report overhead status to measured only
when this evidence is attached; otherwise it must remain unknown.

| System | Instrumentation-off run | Instrumentation-on run | Latency change | CPU change | Callback change | Decision |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Linux | `20260727t101351-513-0600-linux-baseline-48000hz-256f-cd0c858affbf-2c868bbf` | `20260727t101353-298-0600-linux-baseline-48000hz-256f-cd0c858affbf-4021d2b8` | +4.579 ms median (+1.57%) | Unavailable | Unavailable | Latency overhead measured; remaining metrics unavailable |
| Windows | Pending | Pending | Pending | Pending | Pending | Pending |
| macOS | Pending | Pending | Pending | Pending | Pending | Pending |

Windows and macOS calibration remain blocked because no corresponding physical systems are
available to the project owner. This is an acceptance constraint, not evidence that those systems
passed. The Linux reports currently expose no real CPU or callback values, so those fields are not
treated as measured zero overhead.

## Correctness And Equivalence

Before accepting a strategy comparison:

- both runs must pass scenario correctness assertions;
- deterministic output values must match the baseline within the scenario's documented tolerance;
- the GUI must report the runs as comparable with no material-field mismatches; and
- the reviewer must record the baseline run ID, candidate run ID, strategy parameters, and result.

| System | Baseline run | Candidate run | Correctness | Output equivalence | Comparable | Reviewer |
| --- | --- | --- | --- | --- | --- | --- |
| Linux | `20260727t100932-557-0600-linux-baseline-48000hz-256f-cd0c858affbf-a3d5a125` | `20260727t100930-787-0600-linux-parameterized-fixture-48000hz-256f-cd0c858affbf-279c0efb` | Passed | Passed | Passed | Derek, 2026-07-27 |
| Windows | Pending | Pending | Pending | Pending | Pending | Pending |
| macOS | Pending | Pending | Pending | Pending | Pending | Pending |

## End-To-End Acceptance

On every reference system, verify each item and retain the exported JSON reports:

- configure valid and invalid runs and confirm inline validation;
- complete a run and observe stabilization, warm-up, trial, and completion progress;
- cancel a separate run and verify completed trials are retained as incomplete;
- close and reopen the application, then reopen the saved completed and cancelled results;
- compare compatible baseline and strategy runs;
- compare an intentionally incompatible pair and confirm labels are blocked with mismatches shown;
- export completed and incomplete results and validate their schema, provenance, raw trials,
  summaries, warnings, status, and launch-to-main-window metric.

| System | Completed run | Cancelled run | Reopened | Compared | Export validated | Reviewer and date |
| --- | --- | --- | --- | --- | --- | --- |
| Linux | Pending | Pending | Pending | Pending | Pending | Pending |
| Windows | Pending | Pending | Pending | Pending | Pending | Pending |
| macOS | Pending | Pending | Pending | Pending | Pending | Pending |

The project owner completed the Linux end-to-end workflow on 2026-07-27. Cross-platform hardware
acceptance remains pending because Windows and macOS systems are unavailable.

## Gate Approval

`PRACTICE_TAKES_ENABLE_PERFORMANCE_LAB` remains `OFF` by default. Approval to reconsider that gate
requires completed evidence above, repeatable results on every applicable reference system, the
audio-thread safety review, successful current-schema export/reopen checks, and sign-off from the
performance feature owner. Missing, unknown, or unstable evidence keeps the feature gated.