## Purpose

Defines repository requirements for CodeQL security scanning coverage in CI.

## Requirements

### Requirement: CodeQL runs on pull requests and main
The repository SHALL run a CodeQL security scan on pull requests targeting `main` and on pushes to `main`.

#### Scenario: Pull request opened against main
- **WHEN** a pull request targets `main`
- **THEN** the CodeQL workflow runs and publishes analysis results

#### Scenario: Commit pushed to main
- **WHEN** a commit is pushed to `main`
- **THEN** the CodeQL workflow runs and publishes analysis results

### Requirement: CodeQL scans repository languages
The CodeQL workflow SHALL analyze both C++ and JavaScript/TypeScript code paths present in this repository.

#### Scenario: CodeQL matrix execution
- **WHEN** the CodeQL workflow starts
- **THEN** one job analyzes `cpp` and one job analyzes `javascript-typescript`

### Requirement: CodeQL workflow uses pinned action SHAs
All GitHub Actions used by the CodeQL workflow SHALL be referenced by full commit SHA.

#### Scenario: Reviewing workflow action references
- **WHEN** maintainers inspect `.github/workflows/codeql.yml`
- **THEN** each `uses:` entry references an action commit SHA rather than a mutable tag

### Requirement: CodeQL runs on a recurring schedule
The repository SHALL run CodeQL on a weekly schedule in addition to event-driven scans.

#### Scenario: Weekly scheduled security scan
- **WHEN** the configured weekly cron time is reached
- **THEN** the CodeQL workflow runs and publishes analysis results
