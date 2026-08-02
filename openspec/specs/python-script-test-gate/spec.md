# Python script test gate

### Requirement: Python tests run in CI
The system SHALL run the repository's Python test suite in GitHub Actions on
every pull request and every push to `main` that modifies files under
`tools/scripts/`, and SHALL fail the check if any test fails.

#### Scenario: Failing Python test introduced
- **WHEN** a pull request modifies a file under `tools/scripts/` such that an
  existing or new Python test fails
- **THEN** the Python check fails and blocks merge

#### Scenario: All Python tests pass
- **WHEN** a pull request modifies files under `tools/scripts/` and every Python test
  passes
- **THEN** the Python check passes

### Requirement: Test discovery covers the whole scripts tree
The Python check SHALL discover tests across all of `tools/scripts/`, so a test file
added in any subdirectory is executed without editing the workflow.

#### Scenario: Test added in a new subdirectory
- **WHEN** a `test_*.py` file is added under any directory in `tools/scripts/`
- **THEN** the Python check executes it on the next run with no workflow change

#### Scenario: Existing secrets-manager tests execute
- **WHEN** the Python check runs
- **THEN** `tools/scripts/secrets/test_secrets_manager.py` is among the tests
  executed, rather than being collected by nothing

### Requirement: The check is scoped to Python changes
The Python check SHALL be triggered only by pull requests or pushes that modify
files under `tools/scripts/` or the check's own workflow file, so changes limited to
the C++ application, the services workspace, or documentation do not run it.

#### Scenario: Change outside tools/scripts/ does not trigger the check
- **WHEN** a pull request modifies only files under `src/**` or `services/**`
- **THEN** the Python check workflow does not run

### Requirement: Release version calculation is tested
The version tooling that the release workflow depends on
(`tools/scripts/release/version.py`) SHALL have automated tests covering version
parsing, bump calculation, and reading and writing the `VERSION` file.

#### Scenario: Bump arithmetic regresses
- **WHEN** a change makes a major, minor, or patch bump produce the wrong
  version
- **THEN** the Python check fails before the release workflow can tag a release

#### Scenario: Malformed version string
- **WHEN** version parsing is given a string that is not `MAJOR.MINOR.PATCH`
- **THEN** the tests assert it is rejected rather than silently accepted
