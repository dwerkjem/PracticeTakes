# Architecture at a Glance

The full description is
[`ARCHITECTURE.md`](https://github.com/dwerkjem/PracticeTakes/blob/main/docs/development/architecture/ARCHITECTURE.md),
and the [architecture map](https://dwerkjem.github.io/PracticeTakes/) is a
browsable graph of every file. This page is the five-minute version.

## Layers

| Directory | Holds | May depend on |
|---|---|---|
| `src/bootstrap` | The JUCE application entry and the top-level window | everything below |
| `src/application` | The shell (windows, menus, workspace), settings, theme, and the tool contract every tool is built through | `features` through the registry, `platform` |
| `src/features` | The tools themselves: tuner, spectrogram, harmonics, Score View, Target Note, feedback | `platform`, and the application's tool contract — **never another tool** |
| `src/platform` | Shared infrastructure: audio capture, MIDI capture, the score model and MusicXML importer | nothing above it |
| `src/services` | The feedback intake service (a Cloudflare Worker, TypeScript) | — |
| `src/tests` | C++ tests, mirroring `src/` path for path | |

A tool reaches application state only through the `ToolServices` bundle it is
built with. Adding a tool is one entry in `BuiltInToolCatalog.h` and one in
`BuiltInTools.cpp`, with no shell change. See
[`adding-a-tool.md`](https://github.com/dwerkjem/PracticeTakes/blob/main/docs/development/architecture/adding-a-tool.md).

## Ownership

The main window owns one `MainComponent`, the shell. The shell owns the single
shared audio device manager, the look-and-feel, the settings window and one live
entry per open tool.

Presentation containers (a docked panel, a floating window, a tab) **never own
the tool**. Moving a tool between them reparents it, so it keeps its analysis
running. The tools are declared after the services they borrow, so they are
destroyed first, and a tool can never outlive its services.

## The audio thread

The audio callback runs on the real-time thread and does almost nothing. It
clears the output, reads the atomic mute and gain, measures the peak, and copies
samples into one preallocated queue per tool. Each tool drains its own queue on
a timer on the message thread and does its analysis there, so a slow tool can't
stall capture or another tool.

The MIDI driver callback follows the same discipline. Its queue drops the
*oldest* event on overflow, because no MIDI event is safe to lose.

Details in
[`audio-thread-safety.md`](https://github.com/dwerkjem/PracticeTakes/blob/main/docs/development/performance/audio-thread-safety.md).

## Testable by construction

Logic worth asserting is kept out of JUCE components. That covers layout trees,
state machines, the score model, the engraving layout, MIDI translation and note
sounding, so it can be unit-tested without a display. Most of
`src/platform/score`, `src/platform/midi` and the engraving layer have no JUCE
dependency at all.

## Words

The project's own vocabulary is defined in
[`CONTEXT.md`](https://github.com/dwerkjem/PracticeTakes/blob/main/docs/development/architecture/CONTEXT.md).
