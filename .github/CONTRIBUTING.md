# Contributing to Practice Takes

Thanks for your interest in contributing. Practice Takes is early-stage
software maintained by a single developer, so process is kept light, but a
few things help keep the codebase consistent.

## Where to start

All build, architecture, and quality documentation lives under
[`docs/development/`](../docs/development/README.md):

- [Building](../docs/development/BUILDING.md) — prerequisites, local
  configuration, and run commands
- [Architecture](../docs/development/ARCHITECTURE.md) — application ownership,
  audio flow, and UI structure
- [Architecture map](https://dwerkjem.github.io/PracticeTakes/) — browsable
  graph of the whole repository with a guided tour, useful for finding where a
  change belongs before you start
- [Design and architecture review checklist](../docs/development/ARCHITECTURE_QA.md) —
  what reviewers check on pull requests beyond formatting/linting
- [QA strategy](../docs/development/QA_STRATEGY.md) — the current CI/testing
  gap analysis and plan
- [Code style](../docs/development/CODE_STYLE.md) — readability and real-time
  audio guidelines
- [Code quality](../docs/development/QUALITY.md) — clang-format, clang-tidy,
  pre-commit, and VS Code diagnostics

Read the sections relevant to what you're changing before opening a pull
request.

## Repository layout

The repository root is kept deliberately small — it holds only the entry
points a newcomer or a tool needs to find immediately:

- `src/` — all source, in any language:
  - `src/bootstrap/`, `src/application/`, `src/features/`, `src/platform/` —
    the C++ application
  - `src/services/` — the Cloudflare Worker services (TypeScript)
  - `src/tests/` — C++ unit tests
- `docs/` — developer documentation, plus `docs/contracts/` for the shared
  wire-format schemas
- `openspec/` — change proposals and specs. It stays at the root because the
  OpenSpec CLI searches *upward* for an `openspec/` directory
- `tools/` — everything that builds, packages, versions, or checks the
  project: `tools/cmake/`, `tools/packaging/`, `tools/scripts/`,
  `tools/secret-patterns`, `tools/VERSION`, and `tools/vcpkg.json`
- `README.md`, `CMakeLists.txt`, `LICENSE`, `CLAUDE.md` — files whose tooling
  requires them at the root. `CLAUDE.md` is a stub that imports
  [`docs/development/AGENT_GUIDE.md`](../docs/development/AGENT_GUIDE.md)

Because the vcpkg manifest is not at the root, `CMakeLists.txt` sets
`VCPKG_MANIFEST_DIR` before `project()`. Configure through CMake rather than
running `vcpkg install` by hand from the root.

Community-health files (`CONTRIBUTING.md`, `SECURITY.md`, `CODEOWNERS`,
issue and pull request templates) live under `.github/`.

Add new files inside one of these directories rather than at the root. If
something seems to need a root entry, check first whether the tool that reads
it actually requires that location.

## Proposing larger changes

Small, single-layer changes that clearly follow an existing pattern can go
straight to a pull request. For anything that touches multiple architectural
layers, introduces a new shared service or ownership pattern, changes the
audio-thread contract, or is otherwise large or ambiguous, this repository
uses the [OpenSpec](../openspec/) workflow (`openspec/changes/`) to write a
proposal, design, specs, and task list before implementation. See
[Design and architecture review checklist § When to write more than a
checklist pass](../docs/development/ARCHITECTURE_QA.md) for the criteria.

## Pull requests

- Follow the pull request template's checklist.
- Run the relevant test suite locally before requesting review
  (`PracticeTakesTests` for C++ changes, `npm run check`/`npm run test`
  from `src/services/` for changes under `src/services/**`).
- `clang-format` runs automatically on commit via pre-commit; `clang-tidy`
  and the services type-check/test suite run automatically on pull
  requests. See [Code quality](../docs/development/QUALITY.md) for details.

## Reporting bugs and requesting features

Use the issue templates offered when opening a new issue. For security
vulnerabilities, see [`SECURITY.md`](SECURITY.md) instead of opening a
public issue.
