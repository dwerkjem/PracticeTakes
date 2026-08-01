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

Keys during a run:

| Key | Does |
|---|---|
| `F1` | Pass this question |
| `F2` | Fail it — **requires a note** |
| `F3` | Skip it |
| `F4` | Can't reach this surface |
| `F5` | **Pass the whole area** — every remaining question on this surface |
| `F6` | Skip the whole area |
| `ctrl+q` | Stop and save |

The note field is focused the whole time, so just type. That is why the verdict
keys are function keys rather than letters: a bare `f` would be typed into the
note instead of failing the question, which made failing impossible — you cannot
supply the required note and press the key if they are the same keystroke.

**`F5` is the one you will use most.** Most surfaces are unaffected by most
changes: look at it, nothing is wrong, one key answers all three axes and moves
on.

Use `F6` rather than `F5` for an area you did not actually examine. Recording a
pass for something unlooked-at is what turns a record into a rubber stamp, and
the two are distinguishable in the record precisely so that distinction survives.

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

Resizing is done with `scripts/quality/ui-validation/window_control`, the same
X11 helper the golden-image harness uses. Resizing a window is not input
synthesis — see below.

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
| `driver.py` | Speaks the control protocol; geometry via `window_control` |
| `app.py` | The Textual UI — deliberately thin |

Everything except `app.py` is standard-library only and tested without a
terminal, a display, or a built binary. The TUI itself is covered headlessly
with Textual's pilot, so it is verified without a human too.
