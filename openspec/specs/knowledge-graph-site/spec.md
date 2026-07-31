# Knowledge graph site

### Requirement: The knowledge graph is published as a static browsable site
The project SHALL publish the Understand-Anything knowledge graph as a static
site on GitHub Pages, reachable without credentials, so that the repository's
architecture can be explored from a link rather than by installing the
Understand-Anything plugin and running a local server.

The published site SHALL be entirely static — HTML, JavaScript, CSS, and JSON
served as files. It SHALL NOT depend on a development server, a request
handler, an access-token gate, or any runtime process, because GitHub Pages
serves files only.

#### Scenario: A visitor opens the site with no local tooling
- **WHEN** a visitor with no clone of the repository and no Node.js
  installation opens the published URL in a browser
- **THEN** the dashboard loads and renders the graph's nodes, edges, layers,
  and tour without prompting for a token or any other credential

#### Scenario: The site is served from a subpath
- **WHEN** the site is published to a GitHub Pages project URL whose path is
  `/PracticeTakes/` rather than the domain root
- **THEN** every asset reference and every data fetch resolves against that
  subpath, and no request is issued to the domain root

#### Scenario: Only a file server is available
- **WHEN** the built output is served by a plain static file server that
  applies no rewriting, no middleware, and no query-parameter handling
- **THEN** the dashboard loads and renders the graph

### Requirement: The site is built from committed graph artifacts
The site build SHALL consume the knowledge graph artifacts already tracked in
the repository under `.ua/`. The build SHALL NOT regenerate the graph, invoke
a language model, or require a model API credential.

Regenerating the graph is a deliberate act performed by a maintainer, not a
consequence of publishing. Graph generation is model-driven: it is
non-deterministic, takes tens of minutes, and consumes paid API credits.
Publishing must remain a cheap, fast, credential-free operation so that it can
run on every graph change without thought.

#### Scenario: A deployment runs
- **WHEN** the publish workflow executes
- **THEN** it reads `.ua/knowledge-graph.json` from the checked-out commit and
  invokes no language model, requires no model API credential, and creates no
  new analysis

#### Scenario: The repository has no model API credential configured
- **WHEN** the publish workflow runs in a repository where no model API
  credential exists
- **THEN** the deployment succeeds

#### Scenario: The graph artifact is absent
- **WHEN** the publish workflow runs against a commit where
  `.ua/knowledge-graph.json` does not exist or does not parse as JSON
- **THEN** the workflow fails with a message naming the missing or invalid
  file, and no site is deployed

### Requirement: Publication is triggered by a change to the graph
Deployment SHALL be triggered by a change to the graph artifacts under `.ua/`
on the default branch, and SHALL additionally be invocable manually. It SHALL
NOT be triggered by pushes that leave the graph unchanged, and pull requests
SHALL NOT deploy.

Concurrent deployments SHALL be serialised so that two runs cannot publish
different graphs at the same time.

#### Scenario: A commit changes only source files
- **WHEN** a commit merges to the default branch changing source code but not
  the graph
- **THEN** no deployment runs, no build occurs, and the published site is
  unchanged

#### Scenario: A maintainer regenerates the graph and commits it
- **WHEN** a maintainer runs the analysis locally and pushes the resulting
  `.ua/` changes to the default branch
- **THEN** a deployment runs and publishes that graph, with no further action
  required from them

#### Scenario: A maintainer republishes on demand
- **WHEN** a maintainer triggers the workflow manually against the default
  branch
- **THEN** a deployment runs even though no graph file changed in the
  triggering event

#### Scenario: Two deployments overlap
- **WHEN** a second deployment is triggered while one is in progress
- **THEN** the runs are serialised and the site's final state is the graph
  from the later commit

#### Scenario: A pull request proposes a graph change
- **WHEN** a pull request changes files under `.ua/`
- **THEN** no deployment runs before the pull request merges

### Requirement: A published graph declares the commit it describes
The published site SHALL make visible which repository commit the graph
describes, and the publish workflow SHALL detect when the graph does not
describe the commit being deployed.

A knowledge graph is a snapshot, and regeneration is manual — so the graph will
routinely lag the code by some number of commits. Published without its
provenance, a stale graph is indistinguishable from a current one, and a
visitor cannot tell whether what they are reading still matches the code.

#### Scenario: The graph matches the deployed commit
- **WHEN** the `gitCommitHash` recorded in the graph's project metadata equals
  the commit being deployed
- **THEN** the deployment proceeds and the site reports the graph as current
  for that commit

#### Scenario: The graph lags the deployed commit
- **WHEN** the recorded `gitCommitHash` is an ancestor of the commit being
  deployed
- **THEN** the deployment still proceeds, the workflow emits a warning naming
  both commits, and the site reports the commit the graph actually describes
  rather than the commit that was deployed

#### Scenario: The graph does not correspond to repository history
- **WHEN** the recorded `gitCommitHash` is absent, malformed, or not a commit
  reachable in the repository
- **THEN** the workflow fails and no site is deployed

### Requirement: Graph regressions are detected before publication
A graph being published SHALL be compared against the graph it replaces, and a
detected structural regression SHALL block publication until a maintainer
confirms the change is intended.

At minimum, the comparison SHALL cover per-type edge counts and total node
counts, and SHALL treat the disappearance of an entire edge type, or a large
proportional drop in any edge type, as a regression.

This requirement exists because the analysis pipeline reports edge removals as
routine corrections. A regeneration that silently discards a whole category of
relationships — for example every test-coverage edge, or every schema edge
pointing into a re-analyzed file — produces a graph that loads cleanly,
validates cleanly, and is quietly wrong. Only a comparison against the prior
graph distinguishes cleanup from damage, and a maintainer reviewing a
multi-megabyte JSON diff by eye will not catch it.

#### Scenario: An edge type disappears entirely
- **WHEN** the graph being published contains zero edges of a type that the
  previously published graph had
- **THEN** the deployment is blocked and the workflow names the lost edge type

#### Scenario: An edge type collapses
- **WHEN** the count of any edge type falls by a large proportion relative to
  the previously published graph
- **THEN** the deployment is blocked and the workflow names the affected type
  with both counts

#### Scenario: Counts drop because scope legitimately shrank
- **WHEN** counts fall because files were deliberately removed from analysis
  scope or deleted from the repository
- **THEN** a maintainer can record that the drop is expected and allow the
  deployment to proceed

#### Scenario: A regeneration adds and refines without losing categories
- **WHEN** counts move within normal variation and no edge type is lost
- **THEN** the deployment proceeds without maintainer intervention

#### Scenario: No previous graph exists
- **WHEN** the first-ever deployment runs and there is nothing to compare
  against
- **THEN** the comparison is skipped and the deployment proceeds

### Requirement: The dashboard build source is pinned by commit
The dashboard application is third-party source from the Understand-Anything
project and is not vendored in this repository. The publish workflow SHALL
obtain it pinned to an immutable Git commit SHA. A branch name, a tag, or a
floating version specifier SHALL NOT be used.

This matches the dependency-pinning practice the root `CMakeLists.txt` already
applies to JUCE and Catch2. It is load-bearing here for a further reason: the
plugin version installed locally (2.9.4) has no corresponding upstream tag or
release asset, so a tag reference is not merely weaker — for some versions it
does not resolve at all.

#### Scenario: Upstream publishes new commits
- **WHEN** the upstream project pushes changes after the pin is set
- **THEN** the workflow continues to build from the pinned commit and the
  published site does not change as a result of the upstream push

#### Scenario: A pin is proposed that is not a commit SHA
- **WHEN** a change sets the dashboard source reference to a branch name, a
  tag, or a version range
- **THEN** the change is rejected in review as non-compliant with this
  requirement

#### Scenario: The pinned commit cannot be fetched
- **WHEN** the pinned commit is unavailable at deployment time
- **THEN** the workflow fails and the previously published site remains in
  place, unmodified

### Requirement: Published third-party code carries its license
The published site redistributes a built bundle of MIT-licensed third-party
source. The deployed output SHALL include that license text and its copyright
notice, reachable from the site.

#### Scenario: The site is deployed
- **WHEN** a deployment completes
- **THEN** the MIT license text and copyright notice covering the bundled
  dashboard source are present in the deployed output and reachable from the
  site's entry page

### Requirement: Publication discloses no secrets
The deployed output SHALL be scanned for secrets before it is published, using
the same pattern set the repository's existing secret scanning applies. A
detection SHALL block the deployment.

The graph contains model-written prose summarising every analysed file, so its
contents are not reviewable by reading the source diff alone. The repository's
`.understandignore` already excludes `.secrets/`, environment files, and
`services/feedback-intake/wrangler.jsonc`; this requirement verifies that
outcome at the point of publication rather than trusting it.

#### Scenario: The built output is clean
- **WHEN** the pre-publish scan finds no match in any file staged for
  deployment, including the graph JSON
- **THEN** the deployment proceeds

#### Scenario: The built output matches a secret pattern
- **WHEN** the pre-publish scan matches a secret pattern anywhere in the
  staged output
- **THEN** the deployment fails, nothing is published, and the previously
  published site remains in place

#### Scenario: An excluded file reaches the staged output
- **WHEN** the staged output contains a node or summary whose file path is
  excluded by `.ua/.understandignore`
- **THEN** the deployment fails and names the offending path

### Requirement: A failed publish does not degrade the site
A deployment that fails at any step SHALL leave the previously published site
intact and serving. A partially built or partially uploaded site SHALL NOT be
published.

#### Scenario: The build step fails
- **WHEN** the dashboard build fails after a previous successful deployment
- **THEN** the previously published site continues to serve unchanged, and the
  workflow reports failure
