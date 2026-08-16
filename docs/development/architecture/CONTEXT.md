# Domain glossary

The vocabulary Practice Takes uses for itself. When naming a type, a test, an
issue, or a proposal, use the term as defined here rather than a synonym.

This is a seed, not a finished glossary. `/domain-modeling` extends it when a
term gets sharpened; add to it rather than inventing parallel language. See
[`../agents/domain.md`](../agents/domain.md) for how the engineering skills
consume it, and [`ARCHITECTURE.md`](ARCHITECTURE.md) for the structures behind
these terms.

## The application

**Tool** — a user-facing analysis feature the shell can open, close, move, and
persist: the tuner, the spectrogram, the harmonic analyzer. A tool is declared
once in `BuiltInToolCatalog.h` and constructed once in `BuiltInTools.cpp`. Not
"component", "panel", "plugin", or "module" — a tool's JUCE `Component` is one
part of it, not the whole.

**Shell** — the application frame that owns tools without knowing what any of
them are: `MainComponent` plus the window, menus, audio state, and workspace
coordination around it. The shell names no individual tool.

**Live tool** — one open *instance* of a tool. Holds its component, its
presentation container, its presentation state, its last saved bounds, and its
settings. A tool can have several live tools only if its instance policy allows
it.

**Presentation** — how a live tool is currently shown: **docked**, **floating**,
or **tabbed**. Changing presentation reparents the component; it never destroys
it, so analysis state and audio registration survive the move.

**Tool services** — the bundle of shared, long-lived services a tool is
constructed with, and its only legitimate route to application state. A tool
never reaches back into the shell.

**Tool catalog** — the JUCE-free declaration of every tool's identity: id,
display name, aliases, instance policy, default size. Distinct from a workspace
catalog.

## Workspaces

**Workspace** — a named, restorable arrangement of live tools: which tools are
open, how they are split or tabbed, which is focused, and each one's settings.

**Workspace catalog** — the collection of saved workspaces plus the active one.
Distinct from the tool catalog; when either could be meant, say which.

**Snapshot** — the persisted form of a workspace. **Capture** turns the runtime
layout into a snapshot; **apply** turns a snapshot back into a runtime layout.
(Note the collision with *capture* in the testing suite below — qualify the word
when the context isn't obvious.)

## Audio

**Audio thread** — the real-time callback. Bounded work only: no allocation,
locks, blocking waits, I/O, logging, UI, or analysis. The hard contract is in
[`../performance/audio-thread-safety.md`](../performance/audio-thread-safety.md).

**Consumer** — anything registered to receive captured samples. Each consumer
gets its own preallocated single-producer/single-consumer FIFO, so one slow
consumer cannot block capture or another consumer.

**Analysis window** — the fixed-size block of samples a tool analyses at once,
drained from its FIFO on a message-thread timer. Analysis happens here, never on
the audio thread.

## Score

**Score** — the JUCE-free normalized model a MusicXML file is imported into.
Headless infrastructure: it has no application consumer yet, and is reached only
from tests.

**Tempo map** — the mapping between musical time and wall-clock time.

**Musical time** — a position expressed in the score's own units (divisions,
measures, beats) rather than in seconds.

**Diagnostic** — a recorded note that the importer dropped or adjusted
something, rather than failing. What the importer accepts, drops with a
diagnostic, or refuses outright is specified in
[`../formats/musicxml-subset.md`](../formats/musicxml-subset.md).

## The testing suite

Vocabulary for the standalone application under `tools/scripts/testing_suite/`,
which is *not* part of Practice Takes. See
[`../quality/TESTING_SUITE.md`](../quality/TESTING_SUITE.md).

**Suite** — one kind of test the project can run: C++ tests, Python script
tests, service tests, the smoke test, benchmarks, golden images, UI capture.

**Run** — one execution of one or more suites on one machine, recorded whole.

**Surface** — a named thing the harness can put on screen and photograph, at a
given geometry and theme.

**Capture** — a screenshot of a surface taken during a run. (Unrelated to
workspace capture above.)

**Verdict** — a human's pass/fail judgement on a capture.

**Measurement** — a recorded number from a run — a timing, a size — always
attributed to the machine that produced it.

**Test control** — the newline-delimited stdin/stdout command channel the
harness drives the running application through, opened only under
`--test-control`. No socket, no port; it dies with the process.
