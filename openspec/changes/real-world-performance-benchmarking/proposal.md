## Why

PracticeTakes needs performance evidence from realistic end-to-end workflows on physical hardware, not only isolated microbenchmarks or developer impressions. A repeatable GUI-driven benchmark mode will make optimization decisions comparable across strategies, machines, and releases while exposing regressions in user-visible responsiveness and real-time audio behavior.

## What Changes

- Add a GUI performance lab that runs repeatable, end-to-end scenarios against a baseline or a selected optimization strategy.
- Capture hardware, operating system, build, audio-device, buffer, sample-rate, and scenario configuration so results remain interpretable.
- Measure user-visible startup and interaction latency, CPU and memory consumption, audio callback stability, dropouts/underruns, and scenario throughput where applicable.
- Support warm-up, repeated trials, progress and cancellation, and statistical summaries instead of relying on a single run.
- Present side-by-side strategy comparisons in the GUI and export machine-readable results for later analysis and regression tracking.
- Keep benchmark orchestration and telemetry away from the real-time audio thread and make instrumentation overhead visible.

## Capabilities

### New Capabilities

- `hardware-performance-lab`: GUI workflows and result contracts for running controlled end-to-end hardware benchmarks and comparing optimization strategies.

### Modified Capabilities

None.

## Impact

- Affects application UI, scenario orchestration, audio/runtime telemetry, system metadata collection, result persistence, and report export.
- Introduces benchmark strategy and scenario interfaces so experimental optimizations can be selected without hard-coding the GUI to one implementation.
- Requires representative physical hardware and audio devices for authoritative results; virtualized CI may validate contracts but cannot replace hardware runs.
- Benchmark instrumentation must preserve normal application behavior and avoid blocking or allocating on the real-time audio callback.