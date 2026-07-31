## 1. Repository configuration

- [ ] 1.1 Enable GitHub Pages with the GitHub Actions source. This is a
      repository-admin action; the workflow cannot do it for itself, and every
      deployment fails until it is done.

## 2. Verification script

- [ ] 2.1 Add `scripts/pages/verify-graph.mjs`, run before any build work.
      Node rather than Python: the job already provisions Node for the Vite
      build, and this repository's Python is a separate in-flight decision.
- [ ] 2.2 Validate that `.ua/knowledge-graph.json` exists, parses, and carries
      the node/edge/layer/tour shape the dashboard requires; fail naming the
      file otherwise.
- [ ] 2.3 Provenance: read `project.gitCommitHash`; fail when absent,
      malformed, or unreachable in history; warn (and continue) when it is an
      ancestor of the deployed commit.
- [ ] 2.4 Regression: compare per-type edge counts and node count against the
      previous revision of the graph, resolved from git history. Fail when an
      edge type present before is now absent, or when any type drops by more
      than the threshold. Skip cleanly when there is no previous revision.
- [ ] 2.5 Excluded paths: fail when any node's `filePath` matches a directory
      or file excluded by `.ua/.understandignore`.
- [ ] 2.6 Accept an override for the regression gate only, so a deliberate
      scope reduction can be published without weakening the other checks.

## 3. Publish workflow

- [ ] 3.1 Add `.github/workflows/pages.yml` triggered by pushes to `main` that
      touch the published graph artifacts, plus `workflow_dispatch`. No
      `pull_request` trigger.
- [ ] 3.2 Serialise with a `concurrency` group and `cancel-in-progress: false`
      — a superseded deployment must not leave the site half-updated.
- [ ] 3.3 Check out with full history; the provenance and regression checks
      both need it.
- [ ] 3.4 Check out the Understand-Anything dashboard at a pinned commit SHA
      into a separate path.
- [ ] 3.5 Build `@understand-anything/core`, then build the dashboard with
      `vite.config.demo.ts` and `--base=/PracticeTakes/`.
- [ ] 3.6 Stage `knowledge-graph.json`, `meta.json`, and `config.json` into the
      build output. Do not stage `fingerprints.json` or `intermediate/`.
- [ ] 3.7 Copy the upstream MIT `LICENSE` into the output and link it from the
      entry page.
- [ ] 3.8 Scan the staged output with gitleaks before upload; a detection fails
      the job.
- [ ] 3.9 Upload and deploy, with `pages: write` and `id-token: write` scoped
      to the deploy job only.

## 4. Verify

- [ ] 4.1 Build locally against the pinned dashboard commit and serve the
      output with a plain static file server; confirm the dashboard renders and
      fetches the graph with no token.
- [ ] 4.2 Confirm the regression gate fires: run it against a graph with an
      edge type removed and check it fails naming that type.
- [ ] 4.3 Confirm the provenance warning fires on a graph whose recorded commit
      is an ancestor of `HEAD`.
- [ ] 4.4 Confirm a push touching only source files triggers no deployment.
- [ ] 4.5 Open the pull request and confirm the workflow runs green, then merge
      and confirm the published URL serves the dashboard.
