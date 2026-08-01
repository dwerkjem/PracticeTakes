# Manual GUI verification

Some things can only be checked by a person looking at the screen: whether the
tuner responds to a real voice, whether a theme applies everywhere, whether a
warning says what to do about it. This harness is how that gets done
consistently and recorded.

It is **not** a checklist you read. It launches the application, drives it to
each surface, asks you about what is on screen, and writes the record itself.
Your only job is to look and answer.

## Running it

Needs a build with the control channel:

```bash
cmake -S . -B build-tc -DCMAKE_BUILD_TYPE=Debug -DPRACTICE_TAKES_ENABLE_TEST_CONTROL=ON
cmake --build build-tc --target PracticeTakes --parallel
```

Then run it through `uv`, which installs the harness's dependency and runs it in
one step:

```bash
uv run scripts/manual_gui --quick     # does it still work?
uv run scripts/manual_gui --full      # before a release
uv run scripts/manual_gui --full --geometry-sweep
uv run scripts/manual_gui --check     # no prompts; proves the harness works
```

**Use `uv run`, not a bare `python3`.** `uv sync` installs into `.venv`, and a
bare `python3` does not look there — so installing the dependency and then
running with `python3` fails with the same "Textual is not installed" message
you were trying to fix. If you would rather not use `uv run`, call the
interpreter in the virtual environment directly:

```bash
.venv/bin/python3 scripts/manual_gui --quick
```

`--check` launches the application, enters every surface, and exits. It asks
nothing, so it is the quick way to find out whether the harness still lines up
with the application after a UI change.

## Answering

You **type** answers rather than pressing hotkeys. Two earlier designs failed
for opposite reasons: bare letters were swallowed by the note field, so a
failure could never be given the reason it requires; function keys are bound by
the terminal's host — VS Code takes F1–F12 — so they never arrived at all. A
typed line collides with nothing and carries the reason in the same breath as
the verdict.

| Type | Does |
|---|---|
| *(nothing)* + Enter | Pass |
| `f <reason>` | Fail — the reason is required, and is typed right here |
| `s` | Skip |
| `a` | **Pass the whole area** — every remaining question on this surface |
| `as` | Skip the whole area |
| `u <reason>` | Can't reach this surface |
| `q` | Stop and save |
| `?` | Show the commands |

`p <note>` and `s <note>` attach a note to a pass or a skip.

**`a` is the one you will use most.** Most surfaces are untouched by most
changes: look at it, nothing is wrong, one keystroke answers all three axes.

Use `as` rather than `a` for an area you did not actually examine. Recording a
pass for something unlooked-at is what turns a record into a rubber stamp, and
the two stay distinguishable in the record precisely so that distinction
survives.

`ctrl+q` also stops and saves, as a safety net.

## Small terminals

The body scrolls and the entry field is docked to the bottom, so the one thing
you must always be able to reach cannot be pushed off-screen by a long question
or a short window. Below about 24 rows the command legend is dropped (`?` still
prints it), and below 18 the diagnostic state line goes too. A narrow terminal
gets a shortened legend.

## What it asks

Every surface is scored on the same three axes:

1. Does it look correct?
2. Does it look well-presented?
3. Does it work?

Fixed axes are what make runs comparable — you can see that presentation
regressed on the tuner between two versions, which a bespoke question set per
surface could not tell you. Surfaces then add their own questions, recorded
separately so they do not dilute that core.

**A failure requires a note.** The run will not advance without one. A recorded
failure with no detail costs more than it saves: it says something is wrong
without saying what, so the surface has to be run again to learn anything.

## Modes

- **`--quick`** — the smallest set that answers "does it still work": the shell,
  the tuner with live input, and the settings window.
- **`--full`** — every surface. What a release is verified with.

The mode is recorded with the run and appears in the filename, so a quick run
can never be mistaken for a release check.

## The geometry sweep

`--geometry-sweep` presents each surface at three window sizes — default,
constrained (800×600), and maximised. The constrained size is below the 900px
threshold at which the title bar collapses to the hamburger menu, so the sweep
also exercises the responsive layout.

Off by default because it triples how many prompts a run asks.

Resizing goes through the control channel, so the application resizes *itself*.
An external resize cannot do this job: the window advertises a 980px minimum
width through `setResizeLimits`, a window manager honours it, and 980 is above
the 900px collapse threshold — so an outside resize can never reach the
collapsed menu. Measured before the fix: an external resize to 800×600 left the
window at 1280×800 while reporting success.

## Nothing is synthesised

The harness never fakes a mouse or a keyboard. It names an approved *state* or
an approved *object*, and the application does the rest:

- `open-state tuner-docked` puts the application into a named configuration.
- `click tools-button` runs that object's own action in process. No pointer
  moves, no button event is posted, nothing is addressed by screen coordinate.

That is why a layout change cannot silently redirect a click, and why a renamed
state fails loudly instead of quietly verifying the wrong thing. The vocabulary
is closed: it lives in `src/application/testcontrol/ApprovedWindowStates.cpp`,
and the harness checks its whole surface list against the application's
`list-states` before a run rather than discovering a mismatch halfway through.

## Records

Each run writes two files to `docs/development/manual-runs/`:

- **JSON** — diffable, so two runs can be compared question by question.
- **Markdown** — what a person reads when asking "what did we verify before
  v0.5.7".

Both name the commit, platform, audio device, mode, and whether the sweep ran.

An interrupted run keeps what was answered and is marked **INCOMPLETE**, in both
forms, prominently. A partial run must never be mistaken for a passing one.

A surface the harness could not reach is recorded as a **failure of that
surface**, not skipped — a surface nobody could look at is a finding.

## Releases are gated on this

`release.yml` fails before building anything if there is no complete, current,
full-mode run for the code being released. That is what makes this harness
load-bearing rather than optional.

**"Current" cannot mean "for this exact commit."** A run verifies the
application built from some commit, and its record is committed *afterwards* —
so a record can never name the commit that contains it. The gate matches on code
state instead: the verified commit must be an ancestor of the release commit,
and no *release-affecting* file may differ between them. Committing a record is
therefore harmless, because it touches nothing that ends up in the binary.

Release-affecting paths are an explicit list in
`scripts/release/check_manual_verification.py`: `src/`, `CMakeLists.txt`,
`cmake/`, `packaging/`, `vcpkg.json`. `docs/`, `tests/`, `scripts/`, and
`services/` are not — none of them changes the desktop binary. `VERSION` is
deliberately excluded, because the release workflow bumps it as part of
releasing and including it would make the gate unsatisfiable for the very case
it guards.

Check it locally before triggering a release:

```bash
python3 scripts/release/check_manual_verification.py
```

### Skipping

Plenty of releases — a docs fix, a CI tweak — do not warrant a full run. The
`workflow_dispatch` inputs `skip_manual_verification` and
`skip_manual_verification_reason` skip the gate; the reason is required, and
both are written to the release's step summary and uploaded as an artifact, so
"which releases shipped unverified, and why" stays answerable later.

The tag-push path has no workflow inputs, so its equivalent is a committed
`.manual-verification-skip` file whose contents are the reason. That asymmetry
is deliberate: skipping on that path is a reviewable commit rather than a
checkbox.

### Waived failures

A full run with a failed item blocks by default. A release can still proceed if
the record carries a waiver naming the surface, the question, and a written
reason:

```json
"waivers": [
  {"surface": "The tuner, docked", "question": "looks-good",
   "reason": "cosmetic, tracked in #113"}
]
```

Waiving is deliberately not something the harness offers mid-run — it is a
decision made after seeing the result, so it is an edit to the record and thus a
reviewable commit. A waiver with an empty reason does not count; otherwise it
would just be a way to silence the gate.

## Keeping it honest

**When an automated test starts covering a question this harness asks, delete
that question in the same change.** The manual run should get shorter as the
suites grow. A question kept "just in case" after it is automated is a tax on
every release, and an item nobody prunes is how a checklist rots into a ritual.

Surfaces are data, in `scripts/manual_gui/surfaces.py`. Adding one is an edit to
that list, not a change to the harness.

## Where things live

| | |
|---|---|
| `surfaces.py` | The surface list and what each asks — data |
| `session.py` | Sequencing: what is asked next, when the app must move |
| `record.py` | The run record and its two rendered forms |
| `driver.py` | Speaks the control protocol, including geometry |
| `app.py` | The Textual UI — deliberately thin |

Everything except `app.py` is standard-library only and tested without a
terminal, a display, or a built binary. The TUI itself is covered headlessly
with Textual's pilot, so it is verified without a human too.
