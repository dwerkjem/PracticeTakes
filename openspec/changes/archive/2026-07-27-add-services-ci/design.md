## Context

`services/feedback-intake` already has working local commands:

```bash
npm run check   # tsc --noEmit
npm run test    # vitest run
```

and `services/package.json` fans these out across workspaces:

```json
"scripts": {
  "check": "npm run check --workspaces --if-present",
  "test": "npm run test --workspaces --if-present"
}
```

No workflow under `.github/workflows/` invokes either. The existing C++
workflows (`cpp-quality-check.yml`, `build-multiplatform.yml`,
`clang-tidy-main.yml`) are scoped with `paths: src/**`/`.clang-*`/etc. and
never touch `services/**`.

## Goals / Non-Goals

**Goals:**
- Every pull request and push touching `services/**` runs `tsc --noEmit`
  and the Vitest suite, blocking merge on failure.
- Reuse the existing workspace-level `check`/`test` aggregator scripts so a
  second service added later under `services/` is covered without editing
  the workflow.

**Non-Goals:**
- No deployment automation (`wrangler deploy`) — that stays manual/separate
  from this change.
- No coverage thresholds or reporting beyond pass/fail — matches the current
  C++ testing bar (no coverage gate there either).
- No changes to `feedback-intake`'s own scripts or dependencies.

## Decisions

**1. One new workflow file, not an addition to an existing C++ workflow.**
None of the existing workflows are scoped to `services/**`, and mixing a
Node.js job into a C++-focused workflow file would couple unrelated failure
domains (matching Decision 3 in the `enforce-cpp-quality-on-prs` change's
design, which made the same call for the C++ PR gate vs. the build/package
workflow).

**2. Invoke `npm run check`/`npm run test` from `services/`, not
`feedback-intake`-specific commands.**
`services/package.json`'s `--workspaces --if-present` aggregation already
exists specifically so CI doesn't need per-service knowledge. Calling into
`feedback-intake` directly would duplicate that indirection and go stale if
a second service is added under `services/` later without a matching CI
update.

**3. Trigger on `pull_request` and `push` to `main`, scoped to
`services/**`.**
Mirrors the existing C++ workflows' path-scoping pattern
(`cpp-quality-check.yml`'s `paths: src/**`, etc.) so unrelated changes (pure
C++ or docs) don't trigger a Node.js job.

**4. Use `npm ci`, not `npm install`.**
Standard CI practice: `npm ci` requires a committed lockfile and installs
exactly what it specifies, avoiding silent dependency drift between local
and CI environments.

## Risks / Trade-offs

- **[Risk]** `services/` currently has one workspace
  (`feedback-intake`); if it's ever removed without removing the workflow,
  the job would run `npm ci`/`check`/`test` in an empty workspace tree and
  succeed vacuously. → Mitigation: out of scope for this change; the C++
  side has the same class of latent risk (`clang-tidy-main.yml`'s glob bug
  was exactly this failure mode) — if this needs a zero-work-discovered
  guard later, add one then, matching the pattern already used for the C++
  workflows.
- **[Trade-off]** Running from `services/` (not `services/feedback-intake/`)
  means a failure in any workspace fails the whole job, not just the
  affected one. Accepted: there is currently only one workspace, and matrix
  branching per-workspace can be added later if/when a second service
  exists and this becomes a real trade-off.
