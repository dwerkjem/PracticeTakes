## 1. Fix recursive file discovery

- [x] 1.1 Replace the `src/*.cpp` / `src/*.h` glob in
      `.github/workflows/clang-tidy-main.yml` (all three invocation points:
      the `--fix` pass, the format pass, and the verification pass) with a
      recursive `find src -type f \( -name '*.cpp' -o -name '*.h' \)` (or
      equivalent) so every nested source file is discovered.
- [x] 1.2 Add an explicit failure when discovery resolves to zero files in
      that workflow (do not let the existing scripts' zero-files-is-success
      behavior mask a future regression).
- [x] 1.3 Update the manual commands documented in
      `docs/development/QUALITY.md` ("Manual clang-tidy use" section) to use
      the same recursive discovery, so local usage matches CI.

## 2. One-time findings pass and cleanup

- [x] 2.1 Run `clang-tidy` (no `--fix`) against every real `.cpp` file
      discovered recursively under `src/`, using the existing `.clang-tidy`
      config, and record the finding count and affected files. **Result:**
      415 `readability-braces-around-statements` findings (non-blocking,
      not in `WarningsAsErrors`) + 6 real `WarningsAsErrors` findings:
      1 `performance-unnecessary-value-param` in
      `MainComponentWorkspaceDrag.cpp`, and 5 `bugprone-narrowing-conversions`
      across `HarmonicAnalyzerComponent.cpp` and `FeedbackComponent.cpp`.
- [x] 2.2 If findings exist in a `WarningsAsErrors` category
      (`clang-analyzer-*`, `bugprone-*`, `performance-*`): resolve them —
      either apply `clang-tidy --fix` for safe fixes and hand-fix the rest,
      or (if a finding reflects an intentional, justified pattern) add a
      narrowly-scoped `// NOLINT` with a comment explaining why, per existing
      project conventions. **Resolved:** changed `beginToolDrag`'s
      `dragImage` parameter to a `const&`; added explicit `static_cast<float>`
      around `int` operands before multiplying by `scale`/`barWidth` in the
      two affected files. The 415 readability findings are left as
      pre-existing, non-blocking style debt (not in `WarningsAsErrors`).
- [x] 2.3 Re-run clang-format across the same recursive file set and commit
      any formatting fallout from 2.2. **Result:** `clang-format --dry-run
      --Werror` across all 20 `.cpp`/`.h` files reports clean; no formatting
      changes were needed.
- [x] 2.4 Confirm a clean re-run: zero `WarningsAsErrors`-category findings
      across the full recursive file set, and `PracticeTakes` /
      `PracticeTakesTests` both still build successfully. **Confirmed:**
      targeted re-run on the 3 touched files shows 0 errors; both targets
      build; `PracticeTakesTests` passes 398 assertions in 75 test cases.

## 3. Add the pull-request quality-gate workflow

- [x] 3.1 Create a new workflow file (e.g.
      `.github/workflows/cpp-quality-check.yml`) triggered on `pull_request`
      for changes touching `src/**`, `.clang-format`, `.clang-tidy`,
      `CMakeLists.txt`, or `cmake/**`.
- [x] 3.2 In that workflow: install clang-format/clang-tidy, configure the
      compilation database, build `PracticeTakes` once (for `JuceHeader.h`,
      matching the post-merge job's existing pattern).
- [x] 3.3 Run `clang-format --dry-run --Werror` (or equivalent check-only
      invocation via `run_clang_format.py`, extended with a check-only mode
      if needed) across the recursive file set; fail the job on any
      non-conforming file.
- [x] 3.4 Run `clang-tidy` without `--fix` across the recursive file set;
      fail the job on any finding in a `WarningsAsErrors` category.
- [x] 3.5 Confirm neither step ever writes/commits changes back to the PR
      branch — check-only in both directions.

## 4. Documentation

- [x] 4.1 Update `docs/development/QUALITY.md` to describe the corrected
      recursive discovery behavior and document the new PR-time quality
      gate (what it checks, that it's check-only, how to run the same
      checks locally before pushing).

## 5. Verification

- [x] 5.1 Confirm the post-merge workflow's fixed discovery actually finds
      all 19+ `.cpp` files (and corresponding `.h` files) under `src/` by
      inspecting the file list it resolves to (dry run / log output).
      **Confirmed:** `find src -type f -name "*.cpp" | sort` locally
      resolves to 20 files (19 pre-existing + none added by this change),
      matching the workflow's discovery command exactly.
- [ ] 5.2 Confirm the new PR workflow runs and passes on a clean branch
      (post-cleanup) and fails when a deliberately unformatted or
      finding-triggering change is introduced on a test branch. Requires
      pushing a branch/opening a PR to exercise the live GitHub Actions run —
      not done locally; pending user go-ahead to push.
- [x] 5.3 Run the existing `PracticeTakesTests` suite to confirm no
      regressions from any Task 2 cleanup changes. **Confirmed:** 398
      assertions in 75 test cases, all passed.
