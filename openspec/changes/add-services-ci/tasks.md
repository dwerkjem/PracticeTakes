## 1. Add the services quality-gate workflow

- [x] 1.1 Create `.github/workflows/services-check.yml` triggered on
      `pull_request` and `push` to `main`, scoped to `paths: services/**`.
- [x] 1.2 In the job: check out the repository, set up Node.js (matching the
      engine expectations implied by `services/feedback-intake/package.json`
      devDependencies, e.g. Node 20+), and run `npm ci` from `services/`.
- [x] 1.3 Run `npm run check` from `services/` (the workspace aggregator,
      not `feedback-intake`-specific commands).
- [x] 1.4 Run `npm run test` from `services/` (the workspace aggregator).

## 2. Verification

- [x] 2.1 Confirm the workflow runs and passes on a clean branch. **Verified
      locally**: `npm ci`, `npm run check` (clean `tsc --noEmit`), and
      `npm run test` (43/43 tests passed across 4 suites) all succeed from
      `services/` on this branch, matching exactly what the workflow runs.
      Live GitHub Actions confirmation still pending a pushed PR.
- [x] 2.2 Confirm the workflow fails when a deliberate type error is
      introduced under `services/feedback-intake/src`, then revert the
      deliberate error. **Verified locally**: appending an invalid
      assignment to `src/index.ts` made `npm run check` fail with `TS2322`;
      reverting restored a clean pass.
- [x] 2.3 Confirm the workflow fails when a deliberate failing test is
      introduced under `services/feedback-intake/test`, then revert the
      deliberate failure. **Verified locally**: appending a failing
      `expect(1).toBe(2)` case to `test/index.test.ts` made `npm run test`
      fail (1 failed / 43 passed); reverting restored 43/43 passing.
- [ ] 2.4 Confirm the workflow does not trigger for a change limited to
      `src/**` or `docs/**`.
