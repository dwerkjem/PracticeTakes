## Purpose

Defines how coverage is measured for C++, TypeScript, and Python, how it is published, and why it is informational rather than gating.

## Requirements

### Requirement: C++ coverage is measured
The build SHALL provide an opt-in coverage configuration that instruments
`PracticeTakesTests` and its sources, and running the suite under that
configuration SHALL produce a machine-readable coverage report attributing
covered and uncovered lines to individual source files under `src/`.

#### Scenario: Coverage build produces a report
- **WHEN** the project is configured with coverage enabled and the test suite is
  run
- **THEN** a coverage report is produced that lists per-file line coverage for
  sources under `src/`

#### Scenario: Coverage is off by default
- **WHEN** the project is configured without the coverage option
- **THEN** no coverage instrumentation is applied and the ordinary build is
  unaffected

### Requirement: Untested sources appear in the report
The C++ coverage report SHALL distinguish source files that are compiled into
the test binary but unexercised from source files that are not compiled into it
at all, so that the reported figure cannot be inflated by omitting a file from
the test target.

#### Scenario: A source file is not in the test target
- **WHEN** a file under `src/` is absent from `add_executable(PracticeTakesTests ...)`
- **THEN** the report identifies it as outside the test build rather than
  silently excluding it from the coverage denominator

### Requirement: TypeScript coverage is measured
The `services/` workspace SHALL provide a command that runs its Vitest suites
with coverage enabled and produces a machine-readable report.

#### Scenario: Worker coverage is produced
- **WHEN** the TypeScript coverage command is run from `services/`
- **THEN** a coverage report covering every workspace is produced

### Requirement: Python coverage is measured
The repository SHALL provide a command that runs the Python test suite under
coverage measurement and produces a machine-readable report for files under
`scripts/`.

#### Scenario: Script coverage is produced
- **WHEN** the Python coverage command is run
- **THEN** a coverage report covering files under `scripts/` is produced

### Requirement: Coverage is published in CI
CI SHALL run coverage for all three languages, publish each report as a
downloadable artifact, and write a human-readable summary of the headline
figures to the workflow step summary.

#### Scenario: A pull request is opened
- **WHEN** a pull request runs CI
- **THEN** coverage artifacts for C++, TypeScript, and Python are available and
  the step summary shows each language's overall figure

### Requirement: Coverage never fails a build
Coverage reporting SHALL be informational for this change. No coverage
threshold, ratchet, or comparison SHALL cause a CI check to fail, and the
absence of a threshold SHALL be stated in the workflow so it is not mistaken for
an oversight.

#### Scenario: Coverage drops sharply
- **WHEN** a pull request reduces measured coverage
- **THEN** the coverage figure is reported and no check fails because of it

### Requirement: A baseline is recorded
The measured starting figures SHALL be recorded in
`docs/development/QA_STRATEGY.md`, replacing the hand-counted estimates in its
coverage areas, together with the date and commit they were measured at.

#### Scenario: The baseline is written down
- **WHEN** coverage is first measured for all three languages
- **THEN** QA_STRATEGY records the per-language figures, the date, and the
  commit, and no longer presents hand-counted line estimates as current
