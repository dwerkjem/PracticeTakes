## ADDED Requirements

### Requirement: Tests execute on Linux arm64 in CI
`PracticeTakesTests` SHALL be executed on the Linux arm64 CI leg on every
pull request and push to main.

#### Scenario: Pull request targets main
- **WHEN** a pull request is opened or updated
- **THEN** `PracticeTakesTests` runs via `ctest` on the `ubuntu-24.04-arm`
  runner and a failing test causes the Linux arm64 job to fail

### Requirement: Tests execute on Windows x64 in CI
`PracticeTakesTests` SHALL be executed on the Windows x64 CI leg on every
pull request and push to main.

#### Scenario: Pull request targets main
- **WHEN** a pull request is opened or updated
- **THEN** `PracticeTakesTests` runs via `ctest` on the `windows-2022`
  runner and a failing test causes the Windows x64 job to fail

### Requirement: Tests execute on Windows arm64 in CI
`PracticeTakesTests` SHALL be executed on the Windows arm64 CI leg on every
pull request and push to main.

#### Scenario: Pull request targets main
- **WHEN** a pull request is opened or updated
- **THEN** `PracticeTakesTests` runs via `ctest` on the `windows-11-arm`
  runner and a failing test causes the Windows arm64 job to fail
