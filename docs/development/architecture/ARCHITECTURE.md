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
- `src/platform` contains shared infrastructure such as microphone capture,
  the normalized score model, and the practice clock (`platform/transport`).

Shell UI helpers are nested under `shell/ui/main_window`, `feedback`, `settings`,
and `workspace`; appearance and audio state live under `shell/state`. This
keeps each directory focused and avoids a single broad application folder.

## Application ownership

`PracticeTakesApplication` owns the main `DocumentWindow`. The main window owns
one `MainComponent`, which acts as the application shell and owns shared global
services.

`MainComponent` owns:

- one `AudioDeviceManager`
- the practice clock, and the transport strip that controls it
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
2. run the practice clock's renderer, which advances the clock and adds the
   metronome's click to the output (before the mute check, so muting the
   microphone does not silence it)
3. read the atomic mute and input-gain controls
4. measure the post-gain peak
5. copy microphone samples into one preallocated SPSC FIFO per active tool
6. return without allocation, locks, logging, file access, analysis, or UI code

The renderer's bounds, and how it is published to the callback, are in
[Audio-thread safety](../performance/audio-thread-safety.md#rendering-the-practice-clock).

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
the tool id — `"tuner"` — and a later one is `"tuner#2"`. The first instance's
id is byte-identical to what the pre-registry code wrote, so `WorkspaceCatalog`
stays at version 1 and existing saved workspaces load unchanged.

Each `ToolDefinition` declares an instance policy of `single` or `multi`, and
the shell enforces it. "Open or focus" focuses a tool's first live instance;
only "Open another" creates a second, and only for a `multi` tool. The three
microphone tools (tuner, spectrogram, harmonic analyzer) are `multi`; the score
view and target note are `single`. An instance can be renamed, and the label is
stored in the workspace's optional `toolLabels`, written only when non-empty.
There are at most six copies of a tool. A tool that reads samples (the
spectrogram and the harmonic analyzer) takes one of `AudioInputService`'s
sixteen preallocated sample streams, so the shell asks `freeConsumerSlots()`
(message thread only) before creating one, and refuses with an explanation of
who holds them. The tuner reads pitch from `SharedPitchAnalysis` and is a
state-only listener (`addStateListener`), which costs no stream. The audio
callback is unchanged. See `openspec/specs/tool-registry/spec.md`.

### Services and lifetime

A tool reaches shared state only through `ToolServices`, passed to its factory.
It is a struct rather than a widening parameter list so a new service is one
field instead of a signature change at every factory. The practice clock
arrived that way, as `ToolServices::clock`.

`MainComponent` holds one `LiveTool` per instance: the component, its docked and
floating chrome, its presentation state, its last floating bounds, and its last
settings payload. The entry outlives the component, so a closed tool reopens
where it was with the settings it had.

**Tools are destroyed before the services they borrow, by construction.**
`liveTools` is declared after `audioInputService` and `appLookAndFeel`, so
reverse-order member destruction tears every tool down first. There is no
runtime check; moving that declaration would silently break the guarantee.

The practice clock has an ordering of its own inside that rule.
`practiceClock` is declared after `audioInputService`, whose callback runs its
renderer, and after `scoreImport`, whose score it plays. Its destructor takes
the renderer back from the callback, waiting for any callback still inside it,
so it has to run while the service still exists. `transportStrip` comes after
`practiceClock`, and `liveTools` after both.

## Practice clock

One clock per application, in `src/platform/transport/`, specified by the
`practice-clock` change. Its position is an integer count of frames of the
callback that captures the microphone, so a captured sample and the clock's
position at the same frame are the same instant.

- **`ClockMath.h`** (JUCE-free) holds every timing rule:
  - the segment table flattened from the score's tempo map and meter (at most
    512 segments);
  - frames ↔ ticks at a tempo scale of 5–200%;
  - bar and beat, and the next click;
  - the loop wrap, by an exact frame count;
  - the count-in;
  - re-anchoring the position when the scale changes.
- **`ClockRenderer`** is the audio-thread half, hosted by the callback. It
  keeps the position, counts in, loops and clicks. It reads only the table and
  commands the message thread publishes to it, never the score.
- **`PracticeClock`** is the message-thread half, owned by `MainComponent` and
  passed to tools as `ToolServices::clock`. It:
  - rebuilds and republishes the table when the score, the scale, the loop or
    the sample rate changes;
  - notifies `ClockListener`s at 60 Hz with a `ClockPosition`, with seeks,
    wraps and the end of the piece delivered once each;
  - publishes the (frame, high-resolution time) pair, so an event from another
    thread, such as a MIDI note, can be placed on the timeline.
- **`TransportStrip`** puts the controls in the window's control strip once a
  score is open.
- **The Score View follows it.** It draws a cursor at the clock's place and
  marks the sounding and upcoming notes. Each laid-out system records its time
  anchors (tick → x) and the score part of each staff, and the JUCE-free
  `engraving/ScoreCursor.h` holds every rule the cursor follows. See the
  archived `score-cursor` change.

The device is opened with two outputs beside its inputs. If that fails, it is
opened input-only and Settings says why the metronome is silent. The clock
keeps time either way. With no device at all, the synthetic-input generator
hands it blocks.

## Takes

A take is recorded against the practice clock (`src/platform/take/`, the
archived `takes` change):

- **The microphone is recorded by the clock's renderer**, inside the audio
  callback, with a stamp of the clock frame of every span. Its audio and the
  clock share one timeline exactly.
- **Keyboard events are placed on that timeline** through the clock's
  (frame, time) pair.
- **`TakeLifecycle`, `Take` and the assembler are JUCE-free.**
- **`TakeRecorder`** is the message-thread service. `MainComponent` owns it,
  after `practiceClock` and `midiInputService` and before `transportStrip` and
  `liveTools`, and passes it to tools as `ToolServices::takes`. While a take
  runs it locks the clock, so nothing it is measured against can change.
- **Takes are kept in memory for the session**, at most 50. Saving them is
  #39's job.

## Evaluation

What a take is judged by lives in `src/platform/evaluation/`. It is
JUCE-free and runs on the message thread after a take, never during one.

- **Onset timing** (the archived `onset-timing` change):
  - the score's expected attacks for one part;
  - onsets from the keyboard's note-ons, or from the microphone by spectral
    flux;
  - a dynamic-programming alignment of the two, in order;
  - a verdict for each attack: early, on time, late or absent, in
    milliseconds and beats, with a tempo-scaled tolerance.

  `judgeTake(take, part)` is the entry point. #48 adds pitch to the same
  alignment, and #52 draws the results on the score.

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

Three of those consumers now exist: the shell's Open Score command, which
imports a file and publishes the result; the Target Note tool, which reads one
note out of it as text; and the Score View tool, which draws it. Drawing lives
in `src/features/score/engraving` — a JUCE-free layout pass producing a
`ScoreLayout` of glyphs with positions in staff spaces — and is documented in
[`notation-engraving.md`](notation-engraving.md). No coordinate reaches the
model: invariant 10 is why the layout exists at all.

### Ownership and immutability

A score is a tree of value types assembled by `ScoreBuilder` and then **frozen**.
`build()` applies every model invariant, repairing violations and recording a
diagnostic for each, and hands back `std::shared_ptr<const Score>`.

`const` is the enforcement mechanism, not a convention. A score is fully built
before it is first shared and is never mutated afterwards, so any number of
readers on any number of threads need no lock and no consumer can mutate a score
another consumer is mid-way through reading. A single owner holds the current
score — the application shell today, the session when #39 lands — and everything
else holds a `shared_ptr<const Score>` copy, so a score stays alive as long as
any reader is using it even if the owner swaps in a different one.

### Who owns the current score, and how it is loaded

`MainComponent` owns one `ScoreImportState`
(`src/application/shell/ui/score/ScoreImportState.h`), which holds the current
`std::shared_ptr<const Score>` and hands it out **by value**. It is declared
before `liveTools`, alongside `audioInputService` and `appLookAndFeel`, so that
a tool reading the current score cannot outlive it — the same reverse-order
destruction argument that governs the audio services.

`ScoreImportState` is JUCE-free and carries one rule worth stating here: a
**failed** import leaves the current score exactly as it was. Closing the score
a user already has open because a second file turned out to be malformed would
be worse than the malformed file.

The load path is the File menu's Open Score command →
`juce::FileChooser` → `ScoreImportJob` (a `juce::Thread`) →
`importMusicXmlFile` → `juce::MessageManager::callAsync` back to
`MainComponent::finishScoreImport`. One import runs at a time; a second request
brings the import window forward rather than starting or cancelling anything.
The window shows the summary `ScoreImportSummary` produces, for a success and a
failure alike, and draws no notation.

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
