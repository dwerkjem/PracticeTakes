## Why

`services/feedback-intake` (a Cloudflare Worker, TypeScript) defines
`npm run check` (`tsc --noEmit`) and `npm run test` (`vitest run`, 4 suites)
plus a workspace-level aggregator at `services/package.json`
(`npm run check --workspaces --if-present`, same for `test`), but no GitHub
Actions workflow runs either command. A PR can merge with a broken type
check or a failing test in this service today with no CI signal at all —
this is the first, lowest-risk item in the QA strategy
(`docs/development/QA_STRATEGY.md` § 1).

## What Changes

- Add a new GitHub Actions workflow that installs dependencies and runs
  `npm run check` and `npm run test` from `services/` on pull requests and
  pushes that touch `services/**`.
- Use the existing workspace aggregator scripts rather than invoking
  `feedback-intake`-specific commands directly, so a future second service
  under `services/` is covered without editing the workflow.
- No changes to application source, build, or release behavior.

## Capabilities

### New Capabilities
- `services-quality-gate`: pull requests and pushes touching `services/**`
  are checked for TypeScript type errors and failing tests before merge.

### Modified Capabilities
(none — no existing spec covers CI for the `services/` workspace)

## Impact

- New file: `.github/workflows/services-check.yml`.
- No changes to `services/feedback-intake` source, `package.json` scripts,
  or `services/package.json` — the existing `check`/`test` scripts are
  reused as-is.
- No changes to the C++ application, its build, or its existing
  `cpp-quality-check.yml`/`clang-tidy-main.yml`/`build-multiplatform.yml`
  workflows.
