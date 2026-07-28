# Contributing to Practice Takes

Thanks for your interest in contributing. Practice Takes is early-stage
software maintained by a single developer, so process is kept light, but a
few things help keep the codebase consistent.

## Where to start

All build, architecture, and quality documentation lives under
[`docs/development/`](docs/development/README.md):

- [Building](docs/development/BUILDING.md) — prerequisites, local
  configuration, and run commands
- [Architecture](docs/development/ARCHITECTURE.md) — application ownership,
  audio flow, and UI structure
- [Design and architecture review checklist](docs/development/ARCHITECTURE_QA.md) —
  what reviewers check on pull requests beyond formatting/linting
- [QA strategy](docs/development/QA_STRATEGY.md) — the current CI/testing
  gap analysis and plan
- [Code style](docs/development/CODE_STYLE.md) — readability and real-time
  audio guidelines
- [Code quality](docs/development/QUALITY.md) — clang-format, clang-tidy,
  pre-commit, and VS Code diagnostics

Read the sections relevant to what you're changing before opening a pull
request.

## Proposing larger changes

Small, single-layer changes that clearly follow an existing pattern can go
straight to a pull request. For anything that touches multiple architectural
layers, introduces a new shared service or ownership pattern, changes the
audio-thread contract, or is otherwise large or ambiguous, this repository
uses the [OpenSpec](openspec/) workflow (`openspec/changes/`) to write a
proposal, design, specs, and task list before implementation. See
[Design and architecture review checklist § When to write more than a
checklist pass](docs/development/ARCHITECTURE_QA.md) for the criteria.

## Pull requests

- Follow the pull request template's checklist.
- Run the relevant test suite locally before requesting review
  (`PracticeTakesTests` for C++ changes, `npm run check`/`npm run test`
  from `services/` for changes under `services/**`).
- `clang-format` runs automatically on commit via pre-commit; `clang-tidy`
  and the services type-check/test suite run automatically on pull
  requests. See [Code quality](docs/development/QUALITY.md) for details.

## Reporting bugs and requesting features

Use the issue templates offered when opening a new issue. For security
vulnerabilities, see [`SECURITY.md`](SECURITY.md) instead of opening a
public issue.
