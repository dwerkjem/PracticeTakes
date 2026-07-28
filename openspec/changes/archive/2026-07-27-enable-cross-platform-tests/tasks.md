## 1. Enable tests on Linux arm64, Windows x64, Windows arm64

- [x] 1.1 In `.github/workflows/build-multiplatform.yml`, change the Linux
      arm64 matrix entry from `run_tests: false` to `run_tests: true`.
- [x] 1.2 Change the Windows x64 matrix entry from `run_tests: false` to
      `run_tests: true`.
- [x] 1.3 Change the Windows arm64 matrix entry from `run_tests: false` to
      `run_tests: true`.

## 2. Verification

- [x] 2.1 Confirm no other changes were introduced (diff should show exactly
      three lines changed in the matrix, nothing else).
- [x] 2.2 Confirm the workflow YAML is still valid after the edits.
- [x] 2.3 Confirm the new test legs pass on a pushed PR (live CI run).
      macOS legs remain `false` and are out of scope for this verification.
