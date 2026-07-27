## Why

`clang-tidy-main.yml`, the only static-analysis gate in this repository, has
never actually analyzed real source files: it invokes
`run_clang_tidy.py src/*.cpp`, but every `.cpp` file in this repo lives at
least one directory below `src/` (e.g. `src/bootstrap/main.cpp`,
`src/features/analysis/tuner/PitchDetector.cpp`). Bash's default glob
behavior passes the literal, non-matching pattern through as a single
argument; both quality scripts silently discard non-existent paths and exit
`0`. The workflow has reported success on every run while analyzing zero
files. Separately, this gate — even once fixed — only runs after merge to
`main`, so a PR can land with unformatted or analyzer-flagged code and no CI
signal ever shows it before merge.

## What Changes

- Fix the glob so `clang-tidy-main.yml` (and any local invocation following
  the documented commands) actually discovers every `.cpp`/`.h` file under
  `src/`, recursively.
- Run clang-tidy once across the real, now-correctly-discovered file set and
  resolve whatever findings surface, so the gate starts from a clean baseline
  rather than immediately blocking on unrelated debt.
- Add a new GitHub Actions workflow that runs on every pull request:
  `clang-format --dry-run --Werror` (fails on any unformatted file) and
  `clang-tidy` in check-only mode (no `--fix`) across the same recursive file
  set, failing the PR check on any finding in an enabled, `WarningsAsErrors`
  category.
- No diff-aware/incremental tooling — the PR gate analyzes the whole
  recursive `src/` tree every run, matching how the fixed post-merge job and
  local manual invocation already work, so there is exactly one clang-tidy
  invocation shape to maintain.

## Capabilities

### New Capabilities
- `cpp-pr-quality-gate`: Every pull request that touches C++ source is
  checked for clang-format compliance and clang-tidy findings (full-repo,
  non-diff-aware) before it can merge, using the same recursive file
  discovery as the existing post-merge clang-tidy job.

### Modified Capabilities
(none — `openspec/specs/` has no existing capability specs for the quality
tooling yet; the post-merge `clang-tidy-main.yml` behavior is being corrected
as a bug fix, not a requirements change, since its intended behavior was
always "analyze all of `src/`")

## Impact

- `.github/workflows/clang-tidy-main.yml` — replace the flat `src/*.cpp`
  glob with recursive file discovery (e.g. `find src -name '*.cpp' -o -name
  '*.h'` or letting the Python scripts walk the tree themselves).
- `scripts/quality/run_clang_tidy.py` / `run_clang_format.py` — evaluate
  whether recursive discovery belongs in the scripts (consistent for local
  use too) or stays workflow-side; either way, behavior must match between
  local docs (`QUALITY.md`) and both workflows.
- New `.github/workflows/*.yml` for the PR-time check (build-multiplatform.yml
  already runs on `pull_request`, but does not run clang-format/clang-tidy at
  all today).
- Whatever source files carry findings once the glob is fixed — scope
  depends on the one-time findings pass; see design.md and tasks.md for the
  resolved count and disposition.
- `docs/development/QUALITY.md` — update to describe the corrected glob
  behavior and the new PR-time gate.
