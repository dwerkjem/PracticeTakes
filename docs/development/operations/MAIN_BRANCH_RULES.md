# Rules on `main`, and what merges itself

`main` carries one ruleset, **main: checks before merge**. It requires a set of
checks to pass before a pull request can merge, and it is what lets GitHub's
auto-merge do the merging: a pull request marked to merge automatically merges
the moment its required checks pass, with nobody watching.

The repository admin is exempt, so pushing a small change straight to `main`
works as it always did. Nothing else is.

## Which pull requests merge themselves

Auto-merge is switched on per pull request, not for the repository. The agent
switches it on when a pull request is one of:

- tests only, documentation only, or OpenSpec bookkeeping (ticking tasks,
  archiving a finished change, syncing specs);
- a bug fix that comes with a test which fails without the fix.

It is left for a person to merge when it is:

- a change someone would notice using the application: interface, sound, MIDI
  or device behaviour — passing CI does not show it works on real hardware;
- a change to CI or the release workflows — a workflow change can pass its own
  checks and still be wrong;
- a dependency bump;
- a new OpenSpec proposal, or a change to a design;
- anything a person has said needs their eyes.

## The required checks

| Check | Workflow | Runs |
|---|---|---|
| Scan commits for secret content | Secret scan | every pull request |
| Reject tracked plaintext secrets | Secret scan | every pull request |
| Linux x64 | Build desktop packages | every pull request |
| Windows x64 | Build desktop packages | every pull request |
| macOS Apple Silicon ARM64 | Build desktop packages | every pull request |
| Format and lint changed C++ | C++ quality check | when C++ or its tooling changed |
| Address and Undefined | Sanitizers | when C++ or its tooling changed |
| Realtime | Sanitizers | when C++ or its tooling changed |
| Test repository scripts | Python quality check | when scripts or `src/` changed |
| Type-check and test services | Services quality check | when `src/services` changed |
| Build feedback-intake container | Services quality check | when `src/services` changed |
| Launch the built application | Smoke tests | when the application changed |

### Why the conditional ones still start on every pull request

A workflow filtered by `on.pull_request.paths` that does not match never starts,
so it never reports — and a required check that never reports blocks the pull
request forever. Every documentation change would wait on a sanitizer that was
never going to run.

So those workflows always start. Their first job,
[`changed-paths.yml`](../../../.github/workflows/changed-paths.yml), asks GitHub
which files the pull request changed and matches them against the list the
workflow used to filter on, with the same meaning
([`changed_paths.py`](../../../tools/scripts/ci/changed_paths.py)). The real
job runs only on a match; otherwise it is skipped, and GitHub counts a skipped
required check as passing.

The decision **fails open**: if it errors, the real job runs. A broken decision
can make a pull request slower, never let it through unchecked.

When a workflow's push filter changes, change the list in its `changes` job to
match — the two are kept side by side for that reason.

## The push token

Two workflows push to `main` themselves: the release workflow (the version
commit and its tag) and the clang-tidy fixer (its fix commits). A brand-new
commit has no checks yet, so the ruleset would refuse both — and GitHub only lets
an app be exempted from a ruleset on an organisation's repository, not a
personal one. So both push with a fine-grained token belonging to the admin,
stored as the repository secret `MAIN_PUSH_TOKEN`. Without it, both fail at
their first step and say so.

A token's push starts workflows where GitHub Actions' own push does not. Two
consequences are handled, and are worth knowing:

- The release tag push would start a second, tag-triggered release of the same
  version. The tag run stands down for any commit whose message starts
  `chore(release): `, which is the commit the dispatch run makes.
- A clang-tidy fix commit would start the fixer again. It stands down for its
  own commit message.

The other effect is wanted: the commits these workflows push now get the same
builds on `main` as any other commit.

### Creating or renewing it

1. GitHub → Settings → Developer settings → Personal access tokens →
   **Fine-grained tokens** → Generate new token.
2. Resource owner: you. Repository access: **Only select repositories** →
   `PracticeTakes`.
3. Permissions → Repository permissions → **Contents: Read and write**.
   Nothing else.
4. Expiration: the longest GitHub offers. Note the date.
5. Store it, from a clone: `gh secret set MAIN_PUSH_TOKEN` and paste the token
   at the prompt.

When it expires, a release fails at *Require the main push token* or at its
push, saying why. Renewing is steps 1–5 again.
