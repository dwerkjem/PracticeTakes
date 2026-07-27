## Context

The repository is maintained by a single person (`@dwerkjem` / Derek R.
Neilson, per `LICENSE`). It has no `SECURITY.md`, `CONTRIBUTING.md`,
`CODEOWNERS`, or `.github/ISSUE_TEMPLATE`/`PULL_REQUEST_TEMPLATE.md`. Its
existing contributor documentation already lives under `docs/development/`
(building, architecture, code style, code quality, the architecture review
checklist added in `enforce-cpp-quality-on-prs`, and the QA strategy).

## Goals / Non-Goals

**Goals:**
- Give security researchers a documented, private way to report a
  vulnerability without opening a public issue.
- Give contributors a single entry point (`CONTRIBUTING.md`) that routes to
  the existing, already-detailed `docs/development/` content instead of
  duplicating it.
- Ensure pull requests automatically request review from the current
  maintainer via `CODEOWNERS`.
- Give issue/PR authors lightweight structure without over-formalizing a
  single-maintainer, early-stage project.

**Non-Goals:**
- No changes to `docs/development/*.md` content — `CONTRIBUTING.md` links to
  it, it doesn't restate it.
- No multi-maintainer review-routing logic in `CODEOWNERS` — one owner, one
  rule (`* @dwerkjem`).
- No CI enforcement of template usage (e.g. no workflow that fails a PR for
  not using the template) — templates are guidance, not a gate.

## Decisions

**1. Vulnerability reports go through GitHub's private security advisory
feature, not a published email address.**
GitHub natively supports "Report a vulnerability" (private security
advisories) when a `SECURITY.md` exists, avoiding the maintenance burden and
spam exposure of publishing a personal or project email address. `SECURITY.md`
will point at this built-in flow.

**2. `CONTRIBUTING.md` is a short router, not a duplicate of
`docs/development/`.**
`docs/development/README.md` already indexes Building, Architecture, the
architecture review checklist, Code style, Code quality, SOPS secrets,
Releasing, and the QA strategy. `CONTRIBUTING.md` links to that index plus a
one-paragraph "how to propose a change" note (mentioning OpenSpec is used
for larger changes, matching current practice) rather than re-explaining
build steps or style rules that already exist and would drift out of sync
if duplicated.

**3. `CODEOWNERS` uses one blanket rule (`* @dwerkjem`).**
There is exactly one maintainer today. A single top-level rule is the
correct scope; per-path ownership rules can be added later if the
contributor base grows.

**4. Issue templates are two focused Markdown forms (bug report, feature
request), not GitHub's newer YAML issue-forms schema.**
Markdown templates are simpler to maintain for a small, single-maintainer
project and match the lightweight tone of the rest of the repo's docs;
YAML issue forms (with typed fields) are more setup for a benefit this
project doesn't need yet. `config.yml` disables the blank-issue option so
every issue goes through one of the two templates.

**5. The PR template is a short checklist, referencing existing docs by
link rather than repeating their content.**
It nudges toward `docs/development/ARCHITECTURE_QA.md` (design/architecture
review) and mentions running the relevant test suite
(`PracticeTakesTests` and/or `services/` `check`/`test`, whichever applies)
— it does not restate the full quality-gate mechanics already documented in
`docs/development/QUALITY.md`.

## Risks / Trade-offs

- **[Trade-off]** A single blanket `CODEOWNERS` rule provides no benefit
  today beyond guaranteeing the maintainer is auto-requested on their own
  PRs (a no-op for a solo maintainer merging their own work), but costs
  nothing to add now and is ready if a second contributor with write access
  joins later.
- **[Risk]** None of these files are enforced by CI (per Non-Goals) — a
  contributor can still ignore the templates entirely. Accepted: matches
  Decision 4/5's "guidance, not a gate" framing; adding enforcement later is
  possible but out of scope for this change.
