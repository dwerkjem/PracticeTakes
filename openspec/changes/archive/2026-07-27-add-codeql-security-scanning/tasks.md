## 1. Workflow implementation

- [x] 1.1 Create `.github/workflows/codeql.yml` with triggers for `push` to `main`, `pull_request` to `main`, weekly schedule, and `workflow_dispatch`.
- [x] 1.2 Configure a matrix for `cpp` and `javascript-typescript`.
- [x] 1.3 Add SHA-pinned actions for checkout, CodeQL init, autobuild/build, and analyze.
- [x] 1.4 For `cpp`, install Linux build dependencies and run CMake configure/build before analysis.

## 2. Validation

- [x] 2.1 Validate workflow syntax and conventions against existing repository CI patterns.
- [x] 2.2 Confirm all action references are SHA pinned.
- [x] 2.3 Run local build/test sanity checks to ensure no regressions from workflow-related edits.
