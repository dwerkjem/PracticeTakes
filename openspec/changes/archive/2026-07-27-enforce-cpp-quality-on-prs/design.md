## Context

`.github/workflows/clang-tidy-main.yml` (post-merge, `main` only) and the
documented manual commands in `docs/development/QUALITY.md` both invoke:

```bash
python3 scripts/quality/run_clang_tidy.py --fix src/*.cpp
python3 scripts/quality/run_clang_format.py src/*.cpp src/*.h
```

Every `.cpp`/`.h` file in this repository lives at least one directory below
`src/` (verified: `find src -maxdepth 1 -name '*.cpp'` returns nothing; the
real files are under `src/application/...`, `src/features/...`,
`src/services/...`, `src/bootstrap/...`). Bash's default (non-`nullglob`)
behavior passes an unmatched glob through as the literal string `src/*.cpp`.
Both `run_clang_tidy.py` and `run_clang_format.py` filter their `files`
argument with `Path(argument).is_file()` (see `run_clang_tidy.py`'s
`source_files = [Path(argument) for argument in options.files if
Path(argument).is_file()]`), silently dropping the non-existent literal path
and returning exit code 0 for zero analyzed files. The workflow's own commit
step (`git diff --quiet -- src`) then reports "clang-tidy did not produce any
source changes" — which is true, but only because nothing was ever
inspected. This gate has reported green on every run while never analyzing
real source.

Separately, `.clang-tidy` already declares `WarningsAsErrors:
clang-analyzer-*, bugprone-*, performance-*` — the intent to hard-fail on
these categories exists in configuration today; it has simply never been
exercised, and never on a pull request (only `build-multiplatform.yml` runs
on `pull_request`, and it does not invoke clang-format or clang-tidy at all).

**One-time findings pass**: before writing this design, clang-tidy was run
locally against the real, recursively-discovered file set
(all 19 `.cpp` files under `src/`) using the existing `.clang-tidy` config.
The full run surfaced 415 `readability-braces-around-statements` findings
(not in `WarningsAsErrors`, non-blocking) and 6 real `WarningsAsErrors`
findings: 1 `performance-unnecessary-value-param` and 5
`bugprone-narrowing-conversions`. All 6 were fixed (see tasks.md Task 2); the
415 readability findings were left as pre-existing, non-blocking style debt.

## Goals / Non-Goals

**Goals:**
- Fix file discovery so post-merge clang-tidy/clang-format analyze every
  real `.cpp`/`.h` file under `src/`, recursively, matching the behavior the
  workflow and docs always intended.
- Resolve whatever findings the corrected discovery surfaces, once, so the
  gate starts from a clean baseline.
- Add a pull-request-time workflow that fails the PR check on
  `clang-format` non-conformance or any `WarningsAsErrors`-category
  clang-tidy finding, using the same recursive, full-repo file discovery as
  the post-merge job — no separate discovery logic to keep in sync.
- Keep exactly one clang-tidy invocation shape (full-repo, no `--line-filter`,
  no diff parsing) across local docs, post-merge job, and PR job.

**Non-Goals:**
- No diff-aware / incremental analysis (e.g. `clang-tidy-diff.py`, `reviewdog`
  line-filtering). Rejected per design discussion: this is a small, actively
  developed codebase where hiding pre-existing findings behind a diff filter
  would defeat the purpose of fixing the discovery bug in the first place.
- No changes to which clang-tidy checks are enabled or which categories are
  `WarningsAsErrors` — that's a separate policy decision from "make the
  existing policy actually run."
- No coverage of the untested build platforms (arm64 Linux, Windows, macOS
  `ctest` gaps) or the TypeScript `feedback-intake` service — explicitly
  deferred, tracked as follow-up ideas from the exploration session, not part
  of this change.

## Decisions

**1. Fix discovery with `find`, not a shell glob, in both the workflow and
the documented manual commands.**
Replace `src/*.cpp` / `src/*.h` with `find src -type f \( -name '*.cpp' -o
-name '*.h' \)` (or equivalent) everywhere the pattern is used: the
post-merge workflow's three invocation points, the new PR workflow, and
`docs/development/QUALITY.md`'s documented manual commands. This keeps the
existing script interface (`run_clang_tidy.py`/`run_clang_format.py` already
accept an arbitrary file list via `argv`) unchanged — only the caller-side
file enumeration changes.

*Alternative considered*: teach the Python scripts themselves to `rglob`
when given a directory instead of a file list (mirroring
`project_uses_generated_juce_header()`'s existing `Path("src").rglob("*")`
pattern in `run_clang_tidy.py`). Rejected for this change: it would silently
change the scripts' documented CLI contract (`files` positional args) and
affect any other caller relying on exact-file-list semantics; a caller-side
`find` fix is smaller, more obviously correct, and easier to verify by
inspection.

**2. Make zero-files-discovered a hard failure, not a silent success.**
Add a check immediately after file discovery in the post-merge and new PR
workflows: if the discovered file list is empty, fail the step explicitly
with a clear message, rather than letting the scripts' current
zero-files-is-success behavior mask a future regression of this same class
of bug (e.g. someone "fixing" the glob again incorrectly, or a directory
rename breaking discovery).

**3. PR gate is a new, separate workflow file, not an addition to
`build-multiplatform.yml` or `clang-tidy-main.yml`.**
`clang-tidy-main.yml` is scoped to `push: branches: [main]` with a
`concurrency` group shared with the release queue — inappropriate for
per-PR, potentially-concurrent runs. `build-multiplatform.yml` already runs
on `pull_request` but is a matrix build/package/test pipeline; bolting
format/lint checks onto it would couple unrelated failure domains (a
formatting nit would block in the same job as a Windows packaging failure).
A new workflow (e.g. `cpp-quality-check.yml`) triggered on `pull_request`
keeps failure domains separable and mirrors the existing pattern of one
workflow per concern.

**4. PR gate runs `clang-format --dry-run --Werror` (check-only) and
`clang-tidy` without `--fix` (check-only) — never commits changes on a PR.**
Auto-fixing on `main` after merge (existing behavior) is fine because it
pushes to a branch under CI's own control. Auto-fixing on a PR branch would
require pushing to a possibly-forked, possibly-protected branch and creates
surprising history changes underneath a human review — check-only, fail on
any diff/finding, is the standard and safer pattern.

**5. Full-repo analysis on every PR run, not just changed files.**
Confirmed during design discussion: given the small codebase size (19
`.cpp` files) and the explicit goal of "the policy that's already declared
in `.clang-tidy` finally actually running," diff-aware tooling was
considered and rejected — it would let findings outside the diff's exact
line ranges (but in touched files, or via cascading includes) go unchecked
indefinitely, and adds a second discovery/mapping mechanism to maintain
alongside the post-merge job's full-repo pass.

## Risks / Trade-offs

- **[Risk]** Fixing discovery may surface a nontrivial number of
  clang-tidy findings across files that have never been analyzed, requiring
  cleanup before the PR gate can be turned on without immediately blocking
  unrelated future PRs. → Mitigation: the one-time findings pass (Task 2) is
  a required prerequisite step, run and resolved before the PR-blocking
  workflow is added; see tasks.md for the concrete disposition.
- **[Risk]** Full-repo clang-tidy analysis on every PR increases CI runtime
  compared to a diff-aware check, on every PR regardless of size. →
  Mitigation: this codebase is small (19 files today); accepted trade-off
  per Decision 5. Revisit if the codebase grows enough that full-repo
  analysis time becomes a real bottleneck.
- **[Trade-off]** Keeping local `docs/development/QUALITY.md` commands,
  the post-merge workflow, and the new PR workflow all using the same
  `find`-based discovery (rather than a shared script/action) means the
  `find` invocation is duplicated three times. Accepted: the invocation is a
  single line, and Decision 1 already rejected centralizing discovery inside
  the Python scripts for this change; a future refactor could extract a
  small shared shell snippet or composite GitHub Action if duplication
  becomes a maintenance problem.

## Migration Plan

No data migration. Rollout sequence (also reflected in tasks.md):
1. Fix discovery in the post-merge workflow and docs.
2. Run clang-tidy/clang-format once locally against the corrected file set;
   resolve any findings in a dedicated cleanup commit/PR.
3. Add the new PR-time workflow, defaulting to full-repo blocking, only
   after step 2's cleanup has landed so the first PR gate run starts clean.

Rollback: revert the new PR workflow file (single file addition) and/or the
discovery-glob fix independently; both are isolated, additive/corrective
changes with no schema or state to unwind.

## Open Questions

None outstanding for this change's scope. Findings-pass disposition (exact
count and files touched, if any) is recorded in tasks.md as the pass
completes, rather than guessed here.
