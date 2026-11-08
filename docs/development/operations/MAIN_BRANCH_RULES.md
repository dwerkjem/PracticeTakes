# Rules on `main`, and what merges itself

`main` carries one ruleset, **main: checks before merge**. It requires a set of
checks to pass before a pull request can merge, and it is what lets GitHub's
auto-merge do the merging: a pull request marked to merge automatically merges
the moment its required checks pass, with nobody watching.

The repository admin is exempt, so pushing a small change straight to `main`
works as it always did. Nothing else is.

## Which pull requests merge themselves

All of them. The agent puts every pull request it opens on auto-merge as soon
as it is opened, Dependabot's included, and nobody has to say "merge". The
required checks below are the whole gate, so a check that matters belongs in
that list rather than in someone's head.

What CI cannot see still merges, so a pull request whose change can only be
confirmed by hand — a MIDI keyboard, a microphone, how something looks — says
in its description what was and was not verified, and the agent says the same
when it reports the pull request. Following it up is a new change, not a held
merge.

An open pull request opened before a workflow change may be missing a check
that is now required, and will wait for it forever. Bringing `main` into its
branch (or asking Dependabot to rebase) makes the current workflows report.

## The required checks

| Check | Workflow | Runs |
|---|---|---|
| Scan commits for secret content | Secret scan | every pull request |
| Reject tracked plaintext secrets | Secret scan | every pull request |
| Linux x64 | Build desktop packages | every pull request; builds unless only documentation changed |
| Windows x64 | Build desktop packages | every pull request; builds unless only documentation changed |
| macOS Apple Silicon ARM64 | Build desktop packages | every pull request; builds unless only documentation changed |
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

### The desktop build skips documentation-only changes differently

The three platform builds skip a pull request that touches nothing they read
(the list is [`desktop_build_paths.txt`](../../../tools/scripts/ci/desktop_build_paths.txt):
everything except documentation, OpenSpec and repository plumbing). Two things
about them differ from the checks above, and both are deliberate:

- **The job runs and its steps skip**, rather than the job skipping. A skipped
  matrix job reports under its unexpanded name, `Linux ${{ matrix.arch }}`, so
  the required `Linux x64` would never report. Each job therefore starts, says
  in its log that there was nothing to build, and passes under its real name.
- **The decision compares commits with git** instead of calling
  `changed-paths.yml`, which needs `pull-requests: read`. The release workflow
  calls the build with `contents: read` only, and a job asking for more would
  stop every release from starting.

The decision fails open the same way: anything but a clear "false" builds. A
push to `main`, a release and a manual run always build.

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
