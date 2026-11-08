## Context

`tool-registry-contract` split a tool's identity (`tuner`) from an instance's
identity (`tuner`, `tuner#2`, ...), keyed the workspace on instance ids, and put
the single-or-multi decision in one function, `ToolInstanceAllocator::nextInstanceId`.
Everything below the menu already copes with several instances of one tool:

- `ToolInstanceAllocator` returns a multi tool's lowest ordinal that is not
  live, so a closed "Tuner 2" frees its slot instead of the count climbing.
- `WorkspaceNormalizer` keeps `tuner#2` only when the tool's policy is `multi`.
- `MainComponent::liveTools` is a vector of `LiveTool` keyed by instance id,
  each with its own layout handle, presentation and saved settings.
- `MainComponent::toolName` already appends the ordinal to every instance
  after the first.

What is missing is the user-facing half, plus one resource limit:

- **The menu.** `showToolsMenu` builds one submenu per registered tool, aimed at
  `instanceIdToOpen(toolId)`. For a multi tool that is the *next* instance, so
  "Close" and the dock/float ticks would point at an instance that isn't open.
- **Names.** Two tuners can only be told apart by ordinal.
- **Audio consumers.** `AudioInputService` has `maximumConsumers = 8` slots,
  each with a preallocated 65,536-sample FIFO, and `addListener` asserts and
  silently returns when they are full. Today the tuner, spectrogram, harmonic
  analyzer and `SharedPitchAnalysis` take four. A user opening more instances
  would run out with no explanation.

## Goals / Non-Goals

**Goals:**

- Two or more live instances of the tuner, spectrogram or harmonic analyzer,
  each with its own settings, analysis, workspace position and name.
- A menu where every live instance is reachable, and where a tool with one
  instance looks exactly as it does now.
- Renaming an instance, persisted with the workspace.
- A cap that is explained to the user, not discovered through an assert.
- No change to the audio callback, the consumer array, or the workspace catalog
  version.

**Non-Goals:**

- Tool categories and favourites (#134, in 1.0).
- A second input device, so "a spectrogram per input" is not possible yet.
- Multi-instance Score View or Target Note.
- Sharing analysis between instances. Each instance analyses for itself, as
  each tool does today. #30 recorded why: isolation between tools is the point.

## Decisions

### Only the microphone tools become multi-instance

The tuner, the spectrogram and the harmonic analyzer flip to `multi` in
`BuiltInToolCatalog.h`. Score View and Target Note stay `single`. Both read the
one `CurrentScore`, so a second copy would show the same notes. A
multi-instance score view becomes worth doing when it can show a different
part, page or zoom, and that is a separate request.

*Alternative rejected:* every tool multi. That adds menu entries for the two
tools where a duplicate is useless.

### The menu lists live instances, and "Open another" creates one

A multi tool's submenu becomes:

```
Tuner ▸  Open or focus
         Open another
         ─────────────
         Tuner          ▸ Focus · Dock in workspace · Float in window · Rename... · Close
         Tuner 2        ▸ Focus · Dock in workspace · Float in window · Rename... · Close
```

With zero or one live instance it stays exactly as today: "Open or focus",
Dock, Float, Close, all aimed at ordinal 1, with "Open another" added. The
nested per-instance list appears only once there are two, so the common case
gains a single line.

"Open or focus" keeps its meaning: focus the first live instance, or open one
if none is. It no longer goes through `instanceIdToOpen`, which for a multi tool
answers "the next free ordinal". The menu item ids, today `tool index × 4 +
offset`, become a table built alongside the menu, mapping each id to an
(instance, action) pair. That avoids an arithmetic scheme that breaks the moment
a submenu's length varies.

*Alternative rejected:* "Open or focus" opens another every time for a multi
tool. That makes the common action create clutter.

### Labels are an optional per-instance field in the workspace

A label is shell state, not a tool's settings, so it doesn't go through the tool
settings codec. Putting it there would make every tool's codec carry a name.
`WorkspaceSnapshot` gains `std::map<std::string, std::string> toolLabels`, keyed
by instance id:

- written only when non-empty;
- read when present;
- capped like `toolSettings` (`maximumToolSettings`, 64);
- each label limited to 40 characters, trimmed, with control characters
  removed.

An empty label means "use the default name". The default stays `toolName`'s
rule: the display name for ordinal 1, display name plus ordinal after that. The
pure rule moves into a `ToolInstanceLabel` helper beside `ToolInstanceAllocator`
so it can be tested headless.

The catalog stays at version 1. The decoder reads properties by name
(`WorkspaceCatalogCodec.cpp`), so an older build ignores `toolLabels`. The labels
are lost if someone downgrades, and nothing else is.

A label lives with the instance in the workspace it was given in. Applying
another workspace that also has a `tuner#2` uses that workspace's label. That is
the same rule as settings.

### The cap is the audio consumer slots, checked before creating

`AudioInputService` gains `freeConsumerSlots()`. It is message-thread only: it
counts empty slots under the same `consumerLock` that `addListener` takes. The
callback, the slot array and its size don't change, so the contract in
`audio-thread-safety.md` is untouched.

The shell checks it before creating an instance of a tool that listens to audio.
A new `ToolDefinition::usesAudioInput` flag, set for the three microphone tools,
tells it which tools those are. When no slot is free, "Open another" is disabled
with the text "Open another (no audio capacity left)". Restoring a workspace
that holds more microphone instances than there are slots opens the instances in
workspace order and leaves the rest closed, the way restoration already skips a
tool it can't open. It does not assert.

Eight slots allow seven microphone tool instances at most, because
`SharedPitchAnalysis` takes one. Each slot is about 256 KB. Raising the count is
a one-line change, but it lengthens the walk the audio callback does, so it
waits for a measurement that says it's needed.

*Open question, recorded rather than solved here:* the tuner and harmonic
analyzer read pitch from `SharedPitchAnalysis`. They register as listeners for
input-state changes, yet each still occupies a FIFO slot it never drains (see
`TunerComponent.cpp`). Giving `AudioInputService` a state-only listener would
free those slots. That changes the platform layer's interface, so it belongs in
its own change. Task 6.1 files the issue.

### Instances are named everywhere through one function

The window title, the tab label, the docked panel header and the menu entry all
call `toolName(instance)`, which returns the label or the default. "Rename..."
is offered in two places: the instance's Tools menu entry, and the tool's own
**...** options menu (`ToolOptionsButton`), which is where someone looking at
one particular tuner reaches for it. Renaming updates all four in place. The tool component is reparented as before, never
recreated.

### Testability

All the rules are pure:

- which instance "Open or focus" targets;
- the default name;
- label validation;
- whether another instance may open, given free slots and the policy;
- the `toolLabels` round trip.

They live in `src/application/tools` and the workspace model, and are tested in
`src/tests/` without a display. The menu and the rename dialog are `MainComponent`
code, outside `PracticeTakesTests`. They are verified with the testing suite's
`two-tuners-split` surface, added to the approved states.

## Risks / Trade-offs

- **The menu grows with instances.** With seven instances open, the Tuner
  submenu has seven entries. → Mitigation: the cap bounds it, and #134 (1.0)
  revisits it. This is the trade-off the maintainer accepted in dropping the
  dependency.
- **Silent `addListener` failure is still reachable** through any path that
  bypasses the shell's check. → Mitigation: only tools add listeners, and only
  the shell creates tools. A test asserts that the shell refuses before
  `addListener` would fail.
- **A workspace naming more microphone instances than there are slots opens
  partially.** → Mitigation: it opens in workspace order, and the layout keeps
  the closed instances' places, as restoration already does for a tool it
  can't open.
- **Labels are user text in the workspace.** → Mitigation: length-capped,
  trimmed, control characters removed, and never interpreted.

## Migration Plan

No migration. Catalog version 1 is unchanged and `toolLabels` is optional. An
existing workspace has no `tool#2` entries and no labels, so it loads exactly as
before, and the existing fixtures are the test of that. To roll back, revert:
an older build reading a newer workspace drops the extra instances through the
existing normalisation, and ignores the labels.

## Open Questions

- The state-only audio listener described above. It is filed as an issue by
  task 6.1, not decided here.
