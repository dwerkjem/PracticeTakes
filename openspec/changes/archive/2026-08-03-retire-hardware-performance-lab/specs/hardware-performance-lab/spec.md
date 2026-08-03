## REMOVED Requirements

### Requirement: Configure a hardware benchmark run
**Reason**: The application no longer contains a benchmarking mode. Measurements
come from the `[.benchmark]` Catch2 cases, run from the testing suite, which is
outside the product and ships nothing.

**Migration**: `uv run test-suite run --kind performance`, or the Benchmarks
suite in the hub.

### Requirement: Execute realistic end-to-end scenarios
**Reason**: The scenario engine — warm-up, repeated trials, cancellation,
aggregation — existed to make in-application runs repeatable. Catch2 already
does the repetition and statistics for the cases that remain, and the suite
records their results.

**Migration**: the `[.benchmark]` cases, and `verification-run-store` for what
becomes of their numbers.

### Requirement: Compare optimization strategies fairly
**Reason**: Comparing two implementations is now comparing two runs, which the
history view draws per metric over time — and which refuses to cross machines
rather than labelling an incomparable pair.

**Migration**: `test-suite-hub` — "History across runs is graphed" and "A trend
never crosses machines".

### Requirement: Capture hardware and run provenance
**Reason**: Provenance moved to the store, where it is stronger: a run attaches
to a machine identified by a hash of stable hardware facts, and volatile detail
is recorded without fragmenting a machine's history.

**Migration**: `verification-run-store` — "Every run records the machine it ran
on" and "Machine identity ignores volatile system detail".

### Requirement: Measure user-visible and real-time performance
**Reason**: The measurements that had users are the benchmark cases, which
remain. `ApplicationLaunchTimer` also remains: the application measures its own
launch in normal operation, and the golden-image validation reads it.

**Migration**: the `[.benchmark]` cases and the launch timing in
`run-ui-golden.zsh`.

### Requirement: Persist and export benchmark results
**Reason**: Results are stored as they are produced and shared as one file per
run through version control, rather than exported by hand from a window.

**Migration**: `test-suite sync`, and `test-suite ingest --performance` for
measurements produced elsewhere — it reads an export by shape, not by producer.
