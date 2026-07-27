## 1. Security policy

- [x] 1.1 Add `SECURITY.md` at the repository root, directing vulnerability
      reports to GitHub's private security advisory ("Report a
      vulnerability") flow.

## 2. Contribution guide

- [x] 2.1 Add `CONTRIBUTING.md` at the repository root linking to
      `docs/development/README.md` and briefly noting that larger changes
      use the OpenSpec workflow already present in `openspec/`.

## 3. Code ownership

- [x] 3.1 Add `CODEOWNERS` at the repository root (or `.github/CODEOWNERS`)
      with a single blanket rule assigning all paths to `@dwerkjem`.

## 4. Issue templates

- [x] 4.1 Add `.github/ISSUE_TEMPLATE/bug_report.md`.
- [x] 4.2 Add `.github/ISSUE_TEMPLATE/feature_request.md`.
- [x] 4.3 Add `.github/ISSUE_TEMPLATE/config.yml` with
      `blank_issues_enabled: false`.

## 5. Pull request template

- [x] 5.1 Add `.github/PULL_REQUEST_TEMPLATE.md` with a short checklist
      linking to `docs/development/ARCHITECTURE_QA.md` and referencing the
      relevant test suite(s) (`PracticeTakesTests` and/or the `services/`
      `check`/`test` scripts).

## 6. Verification

- [x] 6.1 Confirm all new files render correctly as Markdown (no broken
      relative links to `docs/development/` files).
- [x] 6.2 Confirm GitHub recognizes the new issue templates and PR template
      by inspecting the "New issue" and "New pull request" flows after
      pushing.
