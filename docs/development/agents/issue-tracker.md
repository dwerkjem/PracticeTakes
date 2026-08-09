# Issue tracker: GitHub

Issues and specs for this repo live as GitHub issues on
[`dwerkjem/PracticeTakes`](https://github.com/dwerkjem/PracticeTakes). Use the
`gh` CLI for all operations.

## Conventions

- **Create an issue**: `gh issue create --title "..." --body "..."`. Use a heredoc for multi-line bodies.
- **Read an issue**: `gh issue view <number> --comments`, filtering comments by `jq` and also fetching labels.
- **List issues**: `gh issue list --state open --json number,title,body,labels,comments --jq '[.[] | {number, title, body, labels: [.labels[].name], comments: [.comments[].body]}]'` with appropriate `--label` and `--state` filters.
- **Comment on an issue**: `gh issue comment <number> --body "..."`
- **Apply / remove labels**: `gh issue edit <number> --add-label "..."` / `--remove-label "..."`
- **Close**: `gh issue close <number> --comment "..."`

Infer the repo from `git remote -v` — `gh` does this automatically when run inside a clone.

## This repository's label taxonomy

Every issue carries an `area:` and a `type:` label; open work also carries a
`priority:`. Triage state uses the `status:` family — see
[`triage-labels.md`](triage-labels.md).

The stock `bug` and `enhancement` labels were **deleted on purpose**. Use
`type:bug` and `type:feature` instead, and do not recreate the stock pair.

| Family      | Values                                                                                    |
| ----------- | ----------------------------------------------------------------------------------------- |
| `area:`     | `audio`, `score`, `app`, `ui`, `build`, `docs`, `tooling`, `service`, `testing`            |
| `type:`     | `bug`, `feature`, `chore`, `docs`, `test`, `security`, `tracker`                            |
| `priority:` | `p0` (blocks the milestone), `p1` (scheduled), `p2` (can slip)                              |
| `status:`   | `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human` (plus bare `wontfix`)     |

Run `gh label list` for the authoritative set with descriptions.

## Relationship to OpenSpec

GitHub issues are the request surface. They are **not** the design surface.

Per `AGENT_GUIDE.md § Workflow notes`, a change that touches multiple layers,
introduces a new shared service or ownership pattern, or alters the
audio-thread contract needs an OpenSpec proposal under `openspec/changes/`
before implementation. When a skill turns an issue into work, check whether
that threshold is crossed; if it is, the issue's outcome is a proposal, not a
pull request.

Small, single-layer changes that follow an existing pattern go straight to a PR.

## Pull requests as a triage surface

**PRs as a request surface: no.** _(Set to `yes` if this repo treats external PRs as feature requests; `/triage` reads this flag.)_

When set to `yes`, PRs run through the same labels and states as issues, using the `gh pr` equivalents:

- **Read a PR**: `gh pr view <number> --comments` and `gh pr diff <number>` for the diff.
- **List external PRs for triage**: `gh pr list --state open --json number,title,body,labels,author,authorAssociation,comments` then keep only `authorAssociation` of `CONTRIBUTOR`, `FIRST_TIME_CONTRIBUTOR`, or `NONE` (drop `OWNER`/`MEMBER`/`COLLABORATOR`).
- **Comment / label / close**: `gh pr comment`, `gh pr edit --add-label`/`--remove-label`, `gh pr close`.

GitHub shares one number space across issues and PRs, so a bare `#42` may be either — resolve with `gh pr view 42` and fall back to `gh issue view 42`.

## When a skill says "publish to the issue tracker"

Create a GitHub issue. Apply an `area:` and a `type:` label at minimum.

## When a skill says "fetch the relevant ticket"

Run `gh issue view <number> --comments`.

## Wayfinding operations

Used by `/wayfinder`. The **map** is a single issue with **child** issues as tickets.

- **Map**: a single issue labelled `wayfinder:map`, holding the Notes / Decisions-so-far / Fog body. `gh issue create --label wayfinder:map`.
- **Child ticket**: an issue linked to the map as a GitHub sub-issue (`gh api` on the sub-issues endpoint). Where sub-issues aren't enabled, add the child to a task list in the map body and put `Part of #<map>` at the top of the child body. Labels: `wayfinder:<type>` (`research`/`prototype`/`grilling`/`task`). Once claimed, the ticket is assigned to the driving dev.
- **Blocking**: GitHub's **native issue dependencies** — the canonical, UI-visible representation. Add an edge with `gh api --method POST repos/<owner>/<repo>/issues/<child>/dependencies/blocked_by -F issue_id=<blocker-db-id>`, where `<blocker-db-id>` is the blocker's numeric **database id** (`gh api repos/<owner>/<repo>/issues/<n> --jq .id`, _not_ the `#number` or `node_id`). GitHub reports `issue_dependencies_summary.blocked_by` (open blockers only — the live gate). Where dependencies aren't available, fall back to a `Blocked by: #<n>, #<n>` line at the top of the child body. A ticket is unblocked when every blocker is closed.
- **Frontier query**: list the map's open children (`gh issue list --state open`, scoped to the map's sub-issues / task list), drop any with an open blocker (`issue_dependencies_summary.blocked_by > 0`, or an open issue in the `Blocked by` line) or an assignee; first in map order wins.
- **Claim**: `gh issue edit <n> --add-assignee @me` — the session's first write.
- **Resolve**: `gh issue comment <n> --body "<answer>"`, then `gh issue close <n>`, then append a context pointer (gist + link) to the map's Decisions-so-far.

The `wayfinder:*` labels do not exist yet; `/wayfinder` creates them on first
use. They sit outside the `area:`/`type:`/`priority:`/`status:` taxonomy
deliberately, because they describe a session rather than a work item.
