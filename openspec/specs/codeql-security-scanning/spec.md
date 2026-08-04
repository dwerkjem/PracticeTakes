## Purpose

Defines repository requirements for CodeQL security scanning coverage in CI.

## Requirements

### Requirement: CodeQL runs on pull requests and main
The repository SHALL run a CodeQL security scan on pull requests targeting `main` and on pushes to `main`. The scan SHALL be provided by GitHub's CodeQL default setup rather than by a workflow file in `.github/workflows/`.

#### Scenario: Pull request opened against main
- **WHEN** a pull request targets `main`
- **THEN** CodeQL default setup runs and publishes analysis results

#### Scenario: Commit pushed to main
- **WHEN** a commit is pushed to `main`
- **THEN** CodeQL default setup runs and publishes analysis results

### Requirement: CodeQL scans repository languages
CodeQL default setup SHALL be configured with every language GitHub detects in this repository: `actions`, `c-cpp`, `javascript`, `python`, and `typescript`.

#### Scenario: Reviewing the configured languages
- **WHEN** maintainers inspect the repository's code scanning default setup configuration
- **THEN** its language list covers `actions`, `c-cpp`, `javascript`, `python`, and `typescript`

### Requirement: CodeQL runs on a recurring schedule
The repository SHALL run CodeQL on a weekly schedule in addition to event-driven scans.

#### Scenario: Weekly scheduled security scan
- **WHEN** the configured weekly cron time is reached
- **THEN** CodeQL default setup runs and publishes analysis results
