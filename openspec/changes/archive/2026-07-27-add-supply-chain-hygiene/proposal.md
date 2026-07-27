## Why

Two supply-chain risks exist today:

1. **No automated dependency update notices.** Neither GitHub Actions nor npm
   packages in `services/` have Dependabot or Renovate coverage. A
   vulnerability disclosed in an Actions dependency or in a `feedback-intake`
   npm package won't surface until someone notices manually.

2. **GitHub Actions are pinned by mutable tag, not commit SHA.** Every
   workflow uses tags like `actions/checkout@v6`. A tag is a mutable pointer
   — if the upstream repository is compromised and the tag is force-pushed to
   a malicious commit, every subsequent CI run silently pulls that commit
   without any change to the workflow file. SHA-pinning is the standard
   mitigation recommended by OpenSSF Scorecard, GitHub's own hardening guide,
   and OWASP CI/CD security guidance.

## What changes

**New file — `.github/dependabot.yml`:**
- Covers the `github-actions` ecosystem (checks `.github/workflows/`) weekly.
- Covers the `npm` ecosystem scoped to `services/feedback-intake/` weekly.
- Does not cover CMake `FetchContent` — Dependabot has no CMake support, and
  the JUCE/Catch2 pins are already immutable git tags chosen deliberately;
  bumping them stays a manual, documented decision.

**Updated files — all workflow files under `.github/workflows/`:**
- Replace every `actions/*@vN` reference with `actions/*@<commit-sha>`
  keeping the human-readable version as an inline comment
  (e.g. `actions/checkout@d23441a48e516b6c34aea4fa41551a30e30af803 # v6`).
- Five Actions references to update across the existing workflows
  (`checkout@v6`, `cache@v4`, `setup-node@v4`, `upload-artifact@v4`,
  `download-artifact@v5`).

## New capabilities

- `dependency-update-alerts`: Dependabot opens automated PRs when GitHub
  Actions or npm packages have updates or known vulnerabilities.
- `actions-sha-pinning`: All GitHub Actions are pinned to immutable commit
  SHAs, preventing silent execution of tag-moved malicious commits.

## Impact

- No source, build, test, or existing workflow logic changes.
- Dependabot PRs will begin appearing for outdated Actions/npm packages;
  each will go through the normal PR review flow and trigger CI checks.
- SHA-pinned Actions are functionally identical to tag-pinned ones;
  no CI behaviour changes.
