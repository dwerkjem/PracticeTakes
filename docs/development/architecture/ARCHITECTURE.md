# Practice Takes architecture

## Source organization

The source tree groups code first by architectural role, then by feature:

- `src/bootstrap` starts the JUCE application and owns the top-level window.
- `src/application/configuration` persists settings and defines defaults.
- `src/application/theme` owns shared palettes and look-and-feel behavior.
- `src/application/shell` coordinates windows, menus, audio state, appearance,
  and workspace layout. Its `ui` and `state` branches keep immediate folder
  fan-out small. `MainComponent` implementations live beside the shell
  responsibility they implement instead of in one monolithic source file.
- `src/application/tools` defines the contract every analysis tool is declared
  and built through — see [Adding a tool](adding-a-tool.md).
- `src/features` contains user-facing analysis and feedback features.
- `src/platform` contains shared infrastructure such as microphone capture
  and the normalized score model.

Shell UI helpers are nested under `shell/ui/main_window`, `feedback`, `settings`,
and `workspace`; appearance and audio state live under `shell/state`. This
keeps each directory focused and avoids a single broad application folder.

## Application ownership

`PracticeTakesApplication` owns the main `DocumentWindow`. The main window owns
one `MainComponent`, which acts as the application shell and owns shared global
services.

`MainComponent` owns:

- one `AudioDeviceManager`
- the application `LookAndFeel`
- the Settings window
- one live component for each open tool
- a docked panel or floating window that presents each live tool component
- the nonmodal microphone warning card

Keeping one shared `AudioDeviceManager` avoids opening the same microphone
separately for every tool. `AudioInputService` owns the one hardware callback;
the tuner and spectrogram register as consumers of that service while their
tool components are open.

## Main window

The main window intentionally remains small and uncluttered. Its top buttons
provide:

- `File`, currently reserved for future commands
- `Settings`, which opens global appearance and audio-device controls
- `Tools`, which opens, docks, floats, and focuses analysis tools

## Tool workspace

`MainComponent` owns each live tuner or spectrogram component. Presentation
containers never own tool components:

- `DockedToolPanel` embeds a tool in the main workspace.
- `ToolWindow` presents the same tool in an independent resizable window.

Moving between these modes detaches and reparents the existing component, so
its analysis buffers, controls, and shared-audio registration stay intact.
Closing a tool destroys its component deterministically; closing or moving a
presentation container cannot destroy the application-level audio service.
One instance of each current tool is supported. Opening an already-live tool
focuses its existing dock or window.

`MainComponent` is also the cross-window drag container and workspace drop
target. A drag changes no ownership or layout state until a valid target is
dropped, so leaving the workspace or cancelling restores the original
presentation automatically. Edge targets choose horizontal or vertical tiling,
the centre target creates a tab group, and the floating target detaches the
tool. `StretchableLayoutManager` enforces 480-pixel horizontal and 280-pixel
vertical minimum tool sizes around an 8-pixel divider. The Tools menu exposes
the same horizontal, vertical, and tabbed operations without pointer dragging.

## Theme contrast

The shared look-and-feel assigns foreground and background colours for labels,
buttons, tabs, editors, menus, dialogs, tooltips, groups, lists, and sliders.
Light-theme palette contrast is covered by automated WCAG normal-text checks,
preventing controls from inheriting low-contrast dark-theme text defaults.

## Theme propagation

The light/dark choice is stored by `MainComponent`. When it changes,
`MainComponent` updates the shared JUCE look-and-feel and then notifies every
open settings or tool window. Tool components also maintain their own small
palette because their custom graphics are drawn directly rather than entirely
through JUCE controls.

The Harmonic Analyzer shares the same `AudioInputService` as the tuner and
spectrogram. Each 4096-sample frame is pitch-tracked, transformed with a Hann
window, and sampled around the first eight expected harmonics. Amplitudes are
normalized to the strongest partial so weak fundamentals remain readable.
Harmonic energy and peak alignment produce a confidence indicator; unpitched
or noisy frames are shown as uncertain rather than assigned a timbre score.
Spectral centroid is presented only as a descriptive brightness measure.

## Audio-thread boundary

JUCE calls `AudioInputService::audioDeviceIOCallbackWithContext` on the
real-time audio thread. That callback performs only bounded work:

1. clear any requested output buffers
2. read the atomic mute and input-gain controls
3. measure the post-gain peak
4. copy microphone samples into one preallocated SPSC FIFO per active tool
5. return without allocation, locks, logging, file access, analysis, or UI code

Each tool drains only its own FIFO from its message-thread timer and performs
analysis there. A slow tool therefore cannot block capture or prevent another
tool from receiving samples.

Each consumer FIFO holds 65,536 mono samples. If a complete device-callback
block does not fit, that newest block is dropped for only that consumer;
already-buffered samples are preserved in order. Dropped blocks and samples
are counted with lock-free atomics and reported in Settings outside the audio
callback.

Device start/stop and sample-rate or active-input-channel changes are stored
atomically by the device callbacks and delivered to consumers on the service
timer. Consumers discard pending samples when the format changes, preventing
frames from different formats from being analyzed together.

The device-running callbacks, rather than the backend's active-channel bitset,
define whether an input is usable. This matters on ALSA, where an open device
can deliver input while reporting an empty or mismatched channel mask. Healthy
devices are rescanned every 15 seconds; disconnected devices are rescanned
every 2 seconds. Recovery never replaces a backend that still reports itself
open, avoiding a race with its capture thread.

The Settings input-volume control applies a shared 0–200% software gain before
fan-out. The live level meter displays the post-gain peak, and the clipping
state is held briefly so it remains visible without requiring UI work in the
callback.

## Tool registry

Analysis tools are declared, not wired in. `src/application/tools` holds the
contract; `MainComponent` names no tool.

The layer splits in two so most of it can be tested without a display:

- `ToolCatalog` is free of JUCE and holds a tool's identity — id, display name,
  historical aliases, instance policy, settings version, preferred size.
  `WorkspaceNormalizer` and `WorkspaceSnapshotApply` depend on it, and so does
  the test control surface, which previously kept a hand-maintained copy of the
  tool names because the old `ToolType` enum lived behind `JuceHeader`.
- `ToolRegistry` adds the JUCE half: the factory that builds each tool's
  component. It reports drift between the two, so a catalog entry with no
  factory fails at startup rather than becoming a menu item that cannot open.

`builtInToolCatalog()` and `builtInToolRegistry()` are the single registration
site. Adding a tool is one entry in each; nothing in the shell changes.

### Tools and instances

A **tool** is a kind of tool. An **instance** is one live copy. The registry is
keyed by tool id; workspace topology, floating bounds, focus, and settings
payloads are keyed by *instance* id.

`ToolInstanceId` is a string. The first instance of a tool is spelled exactly as
the tool id — `"tuner"` — and a later one would be `"tuner#2"`. Because every
shipped tool is single-instance, every id written today is byte-identical to
what the pre-registry code wrote, so `WorkspaceCatalog` stays at version 1 and
existing saved workspaces load unchanged.

Each `ToolDefinition` declares an instance policy of `single` or `multi`, and
the shell enforces it: opening an already-open single-instance tool focuses it
rather than duplicating it. **Every shipped tool is `single`.** `multi` is
implemented and tested but deliberately unexercised in production — the
deduplication in `WorkspaceNormalizer` and `WorkspaceSnapshotApply` already keys
on instance ids, so enabling a duplicatable tool later does not mean revisiting
the workspace model or migrating the persisted format.

### Services and lifetime

A tool reaches shared state only through `ToolServices`, passed to its factory.
It is a struct rather than a widening parameter list so a new service — a
transport, when reference tempo lands — is one field instead of a signature
change at every factory.

`MainComponent` holds one `LiveTool` per instance: the component, its docked and
floating chrome, its presentation state, its last floating bounds, and its last
settings payload. The entry outlives the component, so a closed tool reopens
where it was with the settings it had.

**Tools are destroyed before the services they borrow, by construction.**
`liveTools` is declared after `audioInputService` and `appLookAndFeel`, so
reverse-order member destruction tears every tool down first. There is no
runtime check; moving that declaration would silently break the guarantee.

Presentation is not ownership. `DockedToolPanel` and `ToolWindow` hold a tool's
chrome, never the tool, which is what lets a tool move between docked and
floating without its analysis restarting or its audio registration dropping.

### Settings

A tool serialises its own settings through `ToolComponent::captureSettings` and
`applySettings`, exchanging an opaque `ToolSettingsPayload` the shell never
interprets. The catalog declares the version; a stored payload at any other
version is discarded and the tool starts at its defaults, so a tool may change
its format freely at the cost of one restore falling back.

Two exceptions remain where the shell still names a tool, both deliberate:
presets are a tuner concept with no general form in the contract, and the stored
`.ptsettings` schema predates the registry and names three tools in fixed
fields. `applyLegacyToolState`/`captureLegacyToolState` in
`MainComponentSettings.cpp` are the single adapter between that schema and the
id-keyed runtime.

## Score model

`src/platform/score` holds the normalized, engraving-independent representation
of a score: parts, measures, voices, notes, chords, rests, pitches with their
notated spelling *and* their sounding MIDI number, clef/key/time changes, tempo
and dynamic directions, and lyric syllables. `src/platform/score/musicxml`
imports MusicXML into it. Nothing here draws anything.

It sits in `platform` rather than `features` because four consumers read it —
the engraved renderer, the score tool, playback, and the session file — across
at least two feature directories, and a `src/features/*` tool may not reach into
another's internals.

### Ownership and immutability

A score is a tree of value types assembled by `ScoreBuilder` and then **frozen**.
`build()` applies every model invariant, repairing violations and recording a
diagnostic for each, and hands back `std::shared_ptr<const Score>`.

`const` is the enforcement mechanism, not a convention. A score is fully built
before it is first shared and is never mutated afterwards, so any number of
readers on any number of threads need no lock and no consumer can mutate a score
another consumer is mid-way through reading. A single owner holds the current
score — the importer's caller today, the session when #39 lands — and everything
else holds a `shared_ptr<const Score>` copy, so a score stays alive as long as
any reader is using it even if the owner swaps in a different one.

Violations are always **a repair plus a diagnostic, never a throw**. A score
arrives from a stranger's file, so "this file is wrong" has to produce something
a musician can still practise with. The trade is explicit: the resulting score is
complete and self-consistent, but no longer a faithful transcription, and the
diagnostic is what says so.

### Import runs on a background thread

Reading, decompressing, and parsing a score is unbounded work with file I/O. It
cannot run on the message thread without freezing the UI. `importMusicXmlFile`
and `importMusicXmlDocument` are plain functions on the caller's thread: they
start no threads, hold no global state, and touch nothing outside their
arguments, so they are testable synchronously and safe to call from a background
thread without further coordination.

### The audio thread never touches a score

Not even to read it. This is a hard rule, stated before any code exists that
could violate it.

A `shared_ptr` copy is a lock-free but *contended* atomic refcount write, and the
score is pointer-chasing over heap nodes with unpredictable cache behaviour.
Neither belongs in a callback that must be bounded, and both would violate the
[audio-thread boundary](#audio-thread-boundary) above.

When playback arrives (#35–#38), the message thread must flatten the score into
a preallocated, POD, contiguous event array and publish that to the audio thread
— the same shape `AudioSampleFifo` already uses to get data across the boundary
in the other direction. The model is designed so that nobody is ever tempted to
read it from a callback because it looked convenient.

### Time

Every duration and position is in **integer ticks at 3840 per quarter note**,
fixed score-wide. 3840 is `2^8 x 15`, so it divides exactly by 2, 3, and 5 —
every power of two up to a 256th note, plus triplets and quintuplets, land on an
integer tick. Source units (`<divisions>`) are rescaled on import, and a
conversion that is not exact emits a diagnostic rather than drifting silently.

`TempoMap` converts between ticks and seconds. It is deliberately standalone —
it knows nothing of `Score`, `Part`, or `Measure` — because #34's MIDI timeline
is a separate model that shares this one time base rather than reinventing it.

See [the supported MusicXML subset](../formats/musicxml-subset.md) for what the
importer reads and what it drops.

## Tuner pipeline

The tuner:

1. copies recent microphone samples into a fixed analysis window
2. calculates RMS input level
3. estimates pitch using zero-padded FFT normalized autocorrelation
4. converts frequency to a fractional MIDI-note value
5. averages and eases the result to reduce visual jitter
6. applies note-switch hysteresis, so the displayed note does not chatter
7. stores recent values for the history graph

The FFT reduces each analysis frame from quadratic to `O(N log N)` work while
preserving the tuner's original peak selection and normalization. Its scratch
buffers are preallocated, and the timer skips analysis when no new microphone
samples have arrived.

The user can display the result as a history graph, horizontal bar, or meter.

## Spectrogram pipeline

The spectrogram:

1. reads one FFT-sized block from its FIFO
2. applies a Hann window
3. performs a frequency-only FFT
4. maps magnitudes to decibels
5. draws a new logarithmically spaced frequency column
6. shifts the existing image left to create a scrolling display

The rendered color mapping differs between light and dark themes, so the graph
remains legible in either appearance.
