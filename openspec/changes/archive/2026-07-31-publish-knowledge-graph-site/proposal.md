## Why

The repository already contains a reviewed map of its own architecture, and
almost nobody can read it.

`.ua/knowledge-graph.json` is tracked in git — 1,096 nodes and 2,885 edges
across 264 files, organised into 10 architectural layers and a 14-step guided
tour. Reading it today requires installing the Understand-Anything plugin,
running `pnpm install` against its dashboard package, building
`@understand-anything/core`, starting a Vite dev server, and opening a
one-time tokenised URL printed to the terminal. That is roughly ten minutes of
setup and a Node toolchain, imposed on anyone who wants to answer "how does
this project fit together?" — including a first-time contributor deciding
whether to attempt an issue, and a reviewer trying to size the blast radius of
a pull request.

The plugin's own shortcut for this does not work. It attempts to fetch a
prebuilt viewer from the upstream GitHub release before falling back to a
local build; the installed version (2.9.4) has no corresponding upstream tag
and no release asset, so that fetch returns 404 on every invocation. There is
also no offline story beyond a local server.

There is a second cost, paid by the maintainer rather than the reader. Every
time the graph is regenerated, publishing the result is a manual chore: build
the bundle, stage the data files, upload them. That chore is exactly the kind
of repetitive mechanical step CI exists to absorb — and the trigger is
unambiguous, because a regenerated graph *is* a change to `.ua/`.

PracticeTakes is a public repository. Its code is already readable by anyone;
the architectural summary of that code is not, purely because of a tooling
requirement. Publishing it costs nothing in disclosure terms and removes the
setup barrier entirely.

The dashboard already supports this. `vite.config.demo.ts` builds with
`VITE_DEMO_MODE=true`, and in that mode `App.tsx` skips the access-token gate
outright and resolves data files relative to `import.meta.env.BASE_URL` instead
of a token-guarded server route. Static hosting is a designed-for path, not a
workaround. This was verified before writing this proposal: a demo-mode build
with `--base=/PracticeTakes/` produced a 3.9 MB bundle in 6.8 seconds, and a
plain `python3 -m http.server` — no Vite, no middleware, no query handling —
served the entry page, the JavaScript bundle, and the full 1.3 MB graph with no
token, returning it intact at 1,096 nodes and 2,885 edges. That is precisely
the serving model GitHub Pages provides.

## What Changes

- Add a GitHub Pages deployment workflow that builds the Understand-Anything
  dashboard in demo mode and publishes it with the repository's committed graph
  artifacts.
- Trigger deployment on pushes to `main` that change `.ua/`, plus manual
  dispatch; never on pull requests. Because regenerating the graph is what
  changes `.ua/`, a maintainer's regeneration-and-commit is the only action
  needed to publish — pushes that leave the graph alone build nothing.
- Build from the graph already committed under `.ua/`. **Graph regeneration
  stays a manual, maintainer-initiated act and does not move into CI.** The
  deploy path therefore invokes no model, needs no model API credential, and
  introduces no per-run API cost, non-determinism, or key-exposure surface.
- Stage only the three files the dashboard reads —
  `.ua/knowledge-graph.json` (required; a load failure is fatal to the page),
  plus `meta.json` and `config.json` (optional; they supply theme and output
  language and are tolerated as absent). The remaining tracked artifacts under
  `.ua/` — `fingerprints.json`, `intermediate/` — are analysis inputs and are
  not published.
- Obtain the dashboard source pinned to an immutable upstream commit SHA
  rather than a branch or tag, matching the SHA pinning that
  `CMakeLists.txt` already applies to JUCE and Catch2.
- Gate deployment on graph provenance: warn when the graph's recorded
  `gitCommitHash` lags the commit being deployed, fail when it is missing or
  unreachable, and surface on the site which commit the published graph
  actually describes.
- Gate deployment on a structural comparison against the previously published
  graph: block publication when an entire edge type disappears or any edge type
  collapses, with a maintainer override for deliberate scope reductions.
- Gate deployment on a secret scan of the built output using the repository's
  existing `secret-patterns` set, and fail if any path excluded by
  `.ua/.understandignore` appears in the staged artifacts.
- Include the upstream MIT license text and copyright notice in the deployed
  output, since publishing a built bundle is redistribution.
- Serialise concurrent runs, and leave the existing site intact when a run
  fails.

Not in scope, deliberately: automating graph regeneration in CI; publishing the
domain graph, diff overlay, or staleness report (all optional, and skipped
entirely in demo mode); a custom domain; vendoring the dashboard source into
this repository; and any change to how the graph is generated locally.

## Capabilities

### New Capabilities

- `knowledge-graph-site`: the repository's knowledge graph is published as a
  credential-free static site, rebuilt automatically whenever the committed
  graph changes, with provenance, regression, secret-scanning, and
  license-attribution gates on every deployment.

### Modified Capabilities

None. The existing `secret-scan.yml` workflow is unchanged; this change reuses
its pattern set against a new artifact rather than altering its requirements.

## Impact

- **`.github/workflows/`** — one new workflow. It needs `pages: write` and
  `id-token: write` permissions, which no existing workflow in this repository
  requests.
- **Repository settings** — GitHub Pages must be enabled with the GitHub
  Actions source. It is currently not enabled
  (`gh api repos/dwerkjem/PracticeTakes/pages` returns 404). This is a
  repository-admin action and cannot be performed by the workflow itself.
- **Maintainer workflow** — regenerating the graph and committing it becomes
  the whole publishing action; there is nothing else to remember. The
  provenance warning makes the resulting lag visible on the site rather than
  silent, so an infrequently-regenerated graph degrades honestly.
- **`.ua/` artifacts** — become a published surface. The graph contains
  model-written prose summarising every analysed file, which is why the
  secret-scan gate applies to the built output and not only to the source
  diff. `.understandignore` already excludes `.secrets/`, environment files,
  and `services/feedback-intake/wrangler.jsonc`.
- **Supply chain** — adds a build-time dependency on
  `Egonex-AI/Understand-Anything` (public, MIT). A raw commit-pinned checkout
  of another repository is not tracked by `dependabot.yml`, so the pin advances
  only when someone deliberately bumps it. That is the intended trade-off, and
  it is the same posture as the JUCE and Catch2 pins.
- **CI cost** — one Node build per graph-changing commit to `main`; measured
  locally at 6.8 seconds for 616 modules, plus dependency installation. No
  model API spend, because no analysis runs here. Node 26 is already pinned in
  `services-check.yml` and should be matched.
- **Output size** — roughly 3.9 MB of bundle and static assets plus a 1.3 MB
  graph, well inside Pages' limits. The main JavaScript chunk exceeds Vite's
  500 kB warning threshold; this is upstream's chunking, not something this
  change introduces or needs to fix.
- **Regression gate** — adds a comparison step that needs read access to the
  previously published graph. Expect it to fire occasionally on legitimate
  scope reductions, which is why it carries an explicit maintainer override
  rather than being advisory-only.
