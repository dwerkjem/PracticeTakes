# Domain docs

How the engineering skills should consume this repository's domain
documentation when exploring the codebase.

**This repository uses non-default paths.** The upstream skills look for
`CONTEXT.md` and `docs/adr/` at the repository root. `AGENT_GUIDE.md` forbids
adding top-level files or directories, so both live under `docs/development/`
instead. Use the paths below, not the defaults.

## Before exploring, read these

- **`docs/development/architecture/CONTEXT.md`** — the domain glossary.
  (Upstream default: `CONTEXT.md` at the repo root.)
- **`docs/development/architecture/adr/`** — architecture decision records.
  Read the ADRs that touch the area you're about to work in.
  (Upstream default: `docs/adr/`.)

Two existing documents carry weight a glossary normally would, and are worth
reading alongside it:

- **`docs/development/architecture/ARCHITECTURE.md`** — ownership, the
  audio-thread boundary, and the source layering. Read this before any change
  touching ownership, the audio thread, or a new tool or service.
- **`docs/development/agents/AGENT_GUIDE.md`** — the hard constraints
  (repository root, audio thread, test layout, version handling).

If any of these files don't exist, **proceed silently**. Don't flag their
absence; don't suggest creating them upfront. The `/domain-modeling` skill
(reached via `/grill-with-docs` and `/improve-codebase-architecture`) creates
them lazily when terms or decisions actually get resolved.

## File structure

This is a **single-context** repository. `src/services/package.json` declares
an npm `workspaces` field, but it holds exactly one package
(`feedback-intake`) — that is a nested workspace, not a monorepo, and does not
warrant a `CONTEXT-MAP.md`.

```
/
├── CLAUDE.md                                  ← stub; imports the agent guide
├── docs/development/
│   ├── agents/
│   │   ├── AGENT_GUIDE.md                     ← hard constraints, commands
│   │   ├── issue-tracker.md
│   │   ├── triage-labels.md
│   │   └── domain.md                          ← this file
│   └── architecture/
│       ├── ARCHITECTURE.md
│       ├── CONTEXT.md                         ← the glossary
│       └── adr/
│           ├── 0001-....md
│           └── 0002-....md
└── src/
```

If this ever becomes a genuine multi-package repository, the multi-context
layout would put a `CONTEXT-MAP.md` beside `CONTEXT.md` in
`docs/development/architecture/`, pointing at one `CONTEXT.md` per context —
still never at the repository root.

## Where a new domain document goes

Nest it. `AGENT_GUIDE.md` is explicit: a new document belongs in the
`docs/development/` subdirectory matching its subject (`build/`,
`architecture/`, `quality/`, `performance/`, `operations/`, `formats/`,
`agents/`), not loose beside the index, and a new subdirectory is the right
answer when a subject grows. Add it to the index in
`docs/development/README.md`.

## Use the glossary's vocabulary

When your output names a domain concept (in an issue title, a refactor
proposal, a hypothesis, a test name), use the term as defined in `CONTEXT.md`.
Don't drift to synonyms the glossary explicitly avoids.

If the concept you need isn't in the glossary yet, that's a signal — either
you're inventing language the project doesn't use (reconsider) or there's a
real gap (note it for `/domain-modeling`).

## Flag ADR conflicts

If your output contradicts an existing ADR, surface it explicitly rather than
silently overriding:

> _Contradicts ADR-0007 (event-sourced orders) — but worth reopening because…_

## ADRs and OpenSpec are different things

They coexist; don't collapse one into the other.

- **OpenSpec** (`openspec/changes/`) is a **forward** proposal: what is about
  to be built, its tasks, and its delta specs. It is a gate before
  implementation, described in `AGENT_GUIDE.md § Workflow notes`.
- **An ADR** (`docs/development/architecture/adr/`) is a **backward** record:
  a decision that was made, the alternatives, and why. It exists so a future
  review doesn't re-litigate settled ground.

An archived OpenSpec change may be worth distilling into an ADR when the
decision it settled would otherwise keep resurfacing. Most won't be.
