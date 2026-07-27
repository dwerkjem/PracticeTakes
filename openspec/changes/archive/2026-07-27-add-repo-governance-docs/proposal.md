## Why

The repository has no documented vulnerability-disclosure process,
contribution guidelines, code-ownership mapping, or issue/PR templates.
Contributors and security researchers currently have no standard place to
report a vulnerability, no guidance on how to contribute, and no structured
way to open issues or PRs consistently. This is item 2 in the QA strategy
(`docs/development/QA_STRATEGY.md` § 5) — independent of other items, lowest
risk, can land any time.

## What Changes

- Add `SECURITY.md` directing vulnerability reports to GitHub's private
  security advisory reporting (Security tab → Report a vulnerability),
  rather than a public issue or an email address.
- Add `CONTRIBUTING.md` pointing contributors at the existing
  `docs/development/` documentation (building, architecture, code style,
  code quality, the architecture review checklist, and the QA strategy)
  rather than duplicating that content.
- Add `CODEOWNERS` mapping the whole repository to the current maintainer
  (`@dwerkjem`), so pull requests automatically request their review.
- Add `.github/ISSUE_TEMPLATE/bug_report.md` and
  `.github/ISSUE_TEMPLATE/feature_request.md` (plus a `config.yml` disabling
  blank issues in favor of the templates).
- Add `.github/PULL_REQUEST_TEMPLATE.md` with a short checklist referencing
  the existing architecture review checklist and testing expectations.

## Capabilities

### New Capabilities
- `repo-governance-docs`: the repository has a documented
  vulnerability-disclosure process, contribution guide, code ownership
  mapping, and structured issue/PR templates.

### Modified Capabilities
(none — no existing spec covers repository governance documentation)

## Impact

- New files only: `SECURITY.md`, `CONTRIBUTING.md`, `CODEOWNERS`,
  `.github/ISSUE_TEMPLATE/bug_report.md`,
  `.github/ISSUE_TEMPLATE/feature_request.md`,
  `.github/ISSUE_TEMPLATE/config.yml`, `.github/PULL_REQUEST_TEMPLATE.md`.
- No changes to application source, build, CI workflows, or existing
  documentation content (only additive links from `CONTRIBUTING.md` back
  into `docs/development/`).
