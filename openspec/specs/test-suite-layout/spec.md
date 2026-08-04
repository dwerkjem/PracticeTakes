## Purpose

Defines the rule that `tests/` mirrors `src/`, what sits outside the mirror, and the check that keeps the tree from drifting back to flat.

## Requirements

### Requirement: The test tree mirrors the source tree
Every unit test file under `tests/` SHALL live at the path formed by replacing
`src/` with `tests/` in the path of the source file it covers, so that a test's
location names the source directory it exercises.

#### Scenario: A test covering a service header
- **WHEN** a test covers `src/services/score/TempoMap.h`
- **THEN** it lives at `tests/services/score/TempoMapTests.cpp`

#### Scenario: A new source directory gains tests
- **WHEN** a source file is added under a `src/` directory that has no
  corresponding `tests/` directory
- **THEN** the matching `tests/` directory is created rather than the test being
  placed at the top level

### Requirement: Shared fixtures live outside the mirror
Fixtures used by tests in more than one area SHALL live under `tests/support/`,
which is exempt from the mirroring rule because such fixtures belong to no
single source directory.

#### Scenario: A fixture used across areas
- **WHEN** a fixture is needed by tests of two different `src/` directories
- **THEN** it lives under `tests/support/` and is included as
  `"support/<Name>.h"`

### Requirement: Both roots are on the test include path
The `PracticeTakesTests` target SHALL place both `src/` and `tests/` on its
include path, so that a test includes its subject by source-relative path and a
shared fixture by `tests/`-relative path, regardless of how deeply the test is
nested.

#### Scenario: A deeply nested test includes a shared fixture
- **WHEN** a test at `tests/features/performance/StrategyRegistryTests.cpp`
  includes `"support/BenchmarkFakes.h"`
- **THEN** the include resolves without a relative path

### Requirement: Suites that mirror no source file have a stated home
Test suites that do not correspond to a single source file — end-to-end,
smoke, and load suites — SHALL live under a named top-level directory in
`tests/` that is documented as intentionally outside the mirror, rather than
being scattered into the mirrored tree.

#### Scenario: A smoke test is added
- **WHEN** an end-to-end smoke test is added
- **THEN** it lives under the documented non-mirrored directory, not beside the
  unit tests of any one source file

### Requirement: The layout is enforced rather than trusted
The repository SHALL provide an automated check that fails when a `.cpp` file
exists directly at the root of `tests/`, or when a mirrored test file has no
corresponding `src/` directory, and this check SHALL run in CI.

#### Scenario: A test is added at the tests root
- **WHEN** a pull request adds a `.cpp` file directly under `tests/`
- **THEN** the layout check fails and reports the offending path

#### Scenario: A conforming test is added
- **WHEN** a pull request adds a test at a path mirroring an existing `src/`
  directory
- **THEN** the layout check passes
