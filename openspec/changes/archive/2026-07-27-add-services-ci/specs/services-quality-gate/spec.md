## ADDED Requirements

### Requirement: Pull requests touching services are type-checked
The system SHALL run a TypeScript type-check (`tsc --noEmit`, via the
workspace `check` script) against every pull request that modifies files
under `services/**`, and SHALL fail the check if type-checking reports any
error.

#### Scenario: Type error introduced under services/
- **WHEN** a pull request modifies a file under `services/feedback-intake/src`
  in a way that introduces a TypeScript type error
- **THEN** the services quality-gate check fails and blocks merge

#### Scenario: Clean type-check
- **WHEN** a pull request modifies files under `services/**` without
  introducing any type error
- **THEN** the services quality-gate check passes

### Requirement: Pull requests touching services run the test suite
The system SHALL run the Vitest suite (via the workspace `test` script)
against every pull request that modifies files under `services/**`, and
SHALL fail the check if any test fails.

#### Scenario: Failing test introduced
- **WHEN** a pull request modifies code under `services/**` such that an
  existing or new Vitest test fails
- **THEN** the services quality-gate check fails and blocks merge

#### Scenario: All tests pass
- **WHEN** a pull request modifies files under `services/**` and all Vitest
  tests pass
- **THEN** the services quality-gate check passes

### Requirement: Check is scoped to the services workspace tree
The system SHALL trigger the services quality gate only for pull requests or
pushes that modify files under `services/**`, so changes limited to the C++
application or documentation do not run this check.

#### Scenario: Change outside services/ does not trigger the gate
- **WHEN** a pull request only modifies files under `src/**` or `docs/**`
- **THEN** the services quality-gate workflow does not run
