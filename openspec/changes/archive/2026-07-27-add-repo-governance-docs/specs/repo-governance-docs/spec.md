## ADDED Requirements

### Requirement: Documented private vulnerability reporting
The repository SHALL include a `SECURITY.md` file that directs anyone
reporting a security vulnerability to GitHub's private security advisory
process, instead of a public issue tracker.

#### Scenario: Security researcher finds SECURITY.md
- **WHEN** a person opens the repository root looking for how to report a
  vulnerability
- **THEN** `SECURITY.md` exists and instructs them to use GitHub's private
  "Report a vulnerability" flow rather than opening a public issue

### Requirement: Contribution guide routes to existing developer docs
The repository SHALL include a `CONTRIBUTING.md` file that links to the
existing `docs/development/` documentation index instead of duplicating its
content.

#### Scenario: New contributor looks for setup instructions
- **WHEN** a contributor opens `CONTRIBUTING.md`
- **THEN** it links to `docs/development/README.md` (or the individual
  Building/Architecture/Code style/Code quality/QA strategy documents) for
  build, style, and quality guidance rather than re-stating that content

### Requirement: Code ownership is mapped for pull requests
The repository SHALL include a `CODEOWNERS` file mapping the entire
repository to the current maintainer, so pull requests automatically
request their review.

#### Scenario: Pull request opened against the repository
- **WHEN** a pull request is opened against any path in the repository
- **THEN** the current maintainer (`@dwerkjem`) is automatically requested
  as a reviewer per `CODEOWNERS`

### Requirement: Issues use structured templates
The repository SHALL provide a bug-report template and a feature-request
template under `.github/ISSUE_TEMPLATE/`, and SHALL disable the option to
open a blank issue.

#### Scenario: Contributor opens a new issue
- **WHEN** a contributor starts creating a new issue
- **THEN** they are offered a bug-report template and a feature-request
  template, and are not offered a blank-issue option

### Requirement: Pull requests use a lightweight checklist template
The repository SHALL provide a `.github/PULL_REQUEST_TEMPLATE.md` that
references the existing architecture review checklist and relevant test
suites without duplicating their full content.

#### Scenario: Contributor opens a new pull request
- **WHEN** a contributor starts creating a new pull request
- **THEN** the PR description is pre-populated with a checklist that links
  to `docs/development/ARCHITECTURE_QA.md` and references running the
  applicable test suite(s)
