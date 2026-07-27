## Why

The repository currently has quality gates for formatting, static analysis, packaging, and benchmark reporting, but it does not yet run a dedicated security scan in CI. We want a baseline, repository-native security signal for C/C++ and JavaScript/TypeScript paths so newly introduced issues are visible in pull requests and main-branch history.

## What changes

- Add `.github/workflows/codeql.yml` using GitHub Advanced Security CodeQL analysis.
- Run scans for both languages present in this repository:
  - `cpp` for the desktop application code under `src/`.
  - `javascript-typescript` for service code under `services/`.
- Trigger on:
  - pull requests targeting `main`
  - pushes to `main`
  - weekly schedule
  - manual dispatch
- Use SHA-pinned actions consistent with current supply-chain policy.
- Build the C++ target before analysis finalization so CodeQL has complete compilation information.

## Impact

- Adds one new CI workflow focused on security scanning.
- Provides PR-level security findings and periodic baseline scanning.
- Does not modify product runtime behavior.
