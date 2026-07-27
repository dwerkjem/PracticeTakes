## ADDED Requirements

### Requirement: Automated dependency update coverage for GitHub Actions
The repository SHALL include a Dependabot configuration covering the
`github-actions` ecosystem so that outdated or vulnerable Actions
dependencies surface as automated pull requests.

#### Scenario: An Actions dependency has an available update
- **WHEN** a GitHub Actions dependency used in `.github/workflows/` has an
  available update or a disclosed vulnerability
- **THEN** Dependabot opens a pull request updating the reference to the
  newer version

### Requirement: Automated dependency update coverage for npm packages
The repository SHALL include Dependabot configuration covering the `npm`
ecosystem for `services/feedback-intake/` so that outdated or vulnerable
npm packages surface as automated pull requests.

#### Scenario: An npm package has an available update
- **WHEN** a package in `services/feedback-intake/package.json` has an
  available update or a disclosed vulnerability
- **THEN** Dependabot opens a pull request updating that package version

### Requirement: GitHub Actions pinned to immutable commit SHAs
All GitHub Actions referenced in workflow files SHALL be pinned to a full
40-character commit SHA rather than a mutable tag, so that a tag being
force-pushed to a different commit does not silently change what code runs
in CI.

#### Scenario: A workflow runs after an upstream tag is force-pushed
- **WHEN** an upstream Actions repository force-pushes a tag to a new commit
- **THEN** the CI workflow continues to execute the commit SHA that was
  explicitly pinned in the workflow file, not the new tag target

#### Scenario: A reviewer inspects a workflow file
- **WHEN** a reviewer reads a pinned `uses:` line in a workflow file
- **THEN** an inline comment (`# vN.x.x` or `# vN`) on the same line
  identifies the human-readable version the SHA corresponds to
