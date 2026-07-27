## ADDED Requirements

### Requirement: Configure a hardware benchmark run
The application SHALL provide a GUI for selecting a benchmark scenario, optimization strategy, warm-up count, measured trial count, and supported audio settings before a run begins.

#### Scenario: Configure a baseline run
- **WHEN** the user selects a scenario and the baseline strategy with valid trial and audio settings
- **THEN** the application enables the benchmark run command and displays the complete run configuration

#### Scenario: Reject an invalid configuration
- **WHEN** the selected scenario, strategy, hardware, or audio settings cannot produce a valid run
- **THEN** the application prevents the run and identifies each setting that must be corrected

### Requirement: Execute realistic end-to-end scenarios
The application SHALL execute benchmark scenarios through the same user-facing application and audio-processing paths used during normal operation, except for deterministic automation and measurement instrumentation.

#### Scenario: Complete repeated trials
- **WHEN** the user starts a valid benchmark configuration
- **THEN** the application performs the configured warm-up runs followed by the configured measured trials and reports progress for each phase

#### Scenario: Cancel a running benchmark
- **WHEN** the user cancels an active benchmark
- **THEN** the application stops after reaching a safe cancellation point, preserves completed trial data as incomplete, and restores normal application operation

### Requirement: Compare optimization strategies fairly
The application SHALL support a baseline and multiple named optimization strategies through a common strategy interface, including lightweight runtime-selectable variants that can be tested from the same working tree and executable, and SHALL mark results comparable only when scenario, workload, hardware, build mode, and runtime settings match.

#### Scenario: Test a lightweight optimization without a branch
- **WHEN** a developer registers a behavior-equivalent runtime strategy variant with a stable identifier and parameters
- **THEN** the GUI can run and compare that variant against the baseline from the same commit and executable without requiring a separate Git branch or build

#### Scenario: Compare compatible strategies
- **WHEN** the user selects completed baseline and optimization runs with matching comparison fields
- **THEN** the GUI displays absolute values and relative changes for each shared metric

#### Scenario: Detect an incompatible comparison
- **WHEN** selected runs differ in a comparison field that can materially affect performance
- **THEN** the GUI identifies the mismatch and does not present the runs as a valid performance improvement or regression

### Requirement: Capture hardware and run provenance
Each benchmark result SHALL record application version and commit, build type, optimization strategy, scenario and workload identity, operating system, CPU, memory, audio device, driver/backend, sample rate, buffer size, and instrumentation version.

#### Scenario: Save run provenance
- **WHEN** a benchmark trial is recorded
- **THEN** its result contains the available provenance values and explicitly marks values that could not be detected

### Requirement: Measure user-visible and real-time performance
The benchmark system SHALL collect scenario-appropriate startup or interaction latency, process CPU, process memory, audio callback timing, deadline misses, and dropout or underrun counts without blocking or allocating on the real-time audio thread.

#### Scenario: Summarize repeated measurements
- **WHEN** all measured trials complete
- **THEN** the application reports sample count, median, tail percentile, minimum, maximum, and variability for applicable continuous metrics and totals for event counts

#### Scenario: Disclose instrumentation overhead
- **WHEN** benchmark results are displayed or exported
- **THEN** the result identifies enabled instrumentation and includes its measured or documented overhead status

### Requirement: Persist and export benchmark results
The application SHALL retain benchmark runs for later comparison and SHALL export a versioned machine-readable report containing raw trial measurements, summaries, configuration, provenance, completion status, and validation warnings.

#### Scenario: Reopen a saved result
- **WHEN** the user opens a previously saved compatible benchmark result
- **THEN** the GUI displays its configuration, provenance, trial measurements, summaries, and warnings without rerunning the scenario

#### Scenario: Export an incomplete run
- **WHEN** the user exports a cancelled or failed run that contains completed trials
- **THEN** the report preserves those trials and clearly records that the run is incomplete and why
