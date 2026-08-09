# Triage labels

The skills speak in terms of five canonical triage roles. This file maps those
roles to the actual label strings used in this repository's issue tracker.

This repository prefixes its labels by family (`area:`, `type:`, `priority:`),
so the four triage-state labels take a `status:` prefix to match. `wontfix` is
the exception: it already existed as a stock GitHub label and is reused as-is
rather than duplicated.

| Label in mattpocock/skills | Label in our tracker     | Meaning                                  |
| -------------------------- | ------------------------ | ---------------------------------------- |
| `needs-triage`             | `status:needs-triage`    | Maintainer needs to evaluate this issue  |
| `needs-info`               | `status:needs-info`      | Waiting on reporter for more information |
| `ready-for-agent`          | `status:ready-for-agent` | Fully specified, ready for an AFK agent  |
| `ready-for-human`          | `status:ready-for-human` | Requires human implementation            |
| `wontfix`                  | `wontfix`                | Will not be actioned                     |

When a skill mentions a role (e.g. "apply the AFK-ready triage label"), use the
corresponding label string from this table.

## Notes for this repository

- Triage labels describe **state**, not classification. They sit alongside the
  `area:` / `type:` / `priority:` labels documented in
  [`issue-tracker.md`](issue-tracker.md) rather than replacing them.
- An issue should carry at most one `status:` label at a time.
- `status:ready-for-agent` means the issue is specified tightly enough to hand
  to an unattended agent. For anything crossing the OpenSpec threshold in
  `AGENT_GUIDE.md § Workflow notes`, that is rarely true until a proposal
  exists — prefer `status:ready-for-human` and note that a proposal is needed.
- The stock `bug` and `enhancement` labels were deleted deliberately. Do not
  reintroduce them; use `type:bug` and `type:feature`.

Edit the right-hand column if the vocabulary changes.
