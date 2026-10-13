## Why

`score-import-command` landed the first caller of the score model: a File menu,
a background import, a summary window, and `ScoreImportState` holding the
imported score as the application's current one. It stopped exactly there. The
comment on that member says so:

> `#33`'s score tool will read the current score ... **Nothing borrows it yet**,
> so this costs nothing today and removes a landmine.

So the score reaches the application and then stops at the shell. No tool can
read it, and nothing derived from a score is drawn anywhere. #187 is the issue
that says this is the gap worth closing next, and it is a `p0`:

- **The tool contract has never carried anything but audio.** `ToolServices`
  holds `AudioInputService&`, `SharedPitchAnalysis&`, and a theme. A score is a
  third kind of shared thing — not a stream, not a device, not a setting — and
  whether it fits the same bundle is the question #187 exists to answer. #25–#29
  (practice tools that know a target note) are all built on the answer.
- **The model's accessors have only ever been exercised by an importer test.**
  Reading a score to display one note is the first time anything asks the model
  for a note the way a *consumer* wants it rather than the way an importer
  produced it. Stage 3 (#13) is 12–16 weeks built on these accessors.
- **The exit criterion #187 actually cares about is a written list of what the
  score model got wrong.** That list cannot be written without a consumer, and
  it is cheaper to write now than after #32's renderer has already absorbed the
  friction and called it normal.

Waiting for #32 (engraving) or #33 (the score view) to be the first tool means
the first consumer of the model is a paint routine — which is the failure mode
`musicxml-import` was written to avoid, because then the model becomes whatever
the paint routine happened to need.

## What Changes

- Add **`score::CurrentScore`**, a JUCE-free holder for "the score the
  application currently has open": publish a `std::shared_ptr<const Score>`,
  read one back by value. `ScoreImportState` owns one and publishes successful
  imports into it, so there is one source of truth rather than two copies of the
  same pointer.
- Carry it in **`ToolServices`** as a `const score::CurrentScore&`, beside
  `audio` and `pitchAnalysis`. This is the seam #187 asks about, and it is one
  new field rather than a signature change at every factory — which is the
  reason `ToolServices` is a struct.
- Add a **Target Note** tool (`src/features/score/`) that reads the current
  score and draws the first target note it finds: the note's spelled name, its
  sounding MIDI number, which part, staff and voice it belongs to, the printed
  bar number it sits in, and where it starts and how long it lasts in seconds.
- Have the tool **notice a score it has not seen** on its own refresh timer,
  by comparing the pointer it holds against the one the holder now has. Opening
  a second score updates an already-open tool without the shell telling it to.
- Make the tool **say what to do when no score is open** rather than drawing an
  empty panel, and say so again when a score is open but holds no pitched note
  at all.
- Add **`score::spelledName`** to the score model: the model stores step, alter
  and octave and has no way to render them, so without this every consumer
  writes its own. The tuner's `noteNameForMidi` is not reusable here — it lives
  inside another tool, and it is MIDI-only, so it cannot tell F♯ from G♭ when
  the file says which.
- Record **`docs/development/architecture/score-model-findings.md`**: what the
  model got wrong, what it got right, and what the next consumer should expect.
  This is #187's real deliverable, and findings become issues under #13.
- Correct the agent guide's claim that `src/platform/score` "has no application
  consumer yet". It was already stale when `score-import-command` landed.

Deliberately **not** in scope:

- Engraving, staff lines, note glyphs, or any notation drawing (#32).
- Navigation, cursors, part selection, zoom, or choosing *which* note is the
  target (#33). The first pitched note of the first part is the target, full
  stop.
- Comparing the target against live audio. It is one line away — the tool
  already holds `pitchAnalysis` — and it is deliberately left undone, because a
  sounding MIDI number becomes a frequency only through a reference pitch, and
  the only reference pitch in the codebase is a `constexpr double` private to
  the tuner. That is a finding to record, not a thing to quietly duplicate.
- Playback, transport, or anything reaching the audio thread (#35–#38).
- Persisting anything per tool instance. The tool has no settings.

## Capabilities

### New Capabilities

- **`score-target-note-tool`** — the current score is reachable from a tool
  through the tool contract, and one target note derived from it is drawn in the
  application.

### Modified Capabilities

- **`score-import-command`** — unchanged in behaviour. The current score it
  already owns is now published somewhere a tool can read it, and the rule it
  exists to enforce (a failed import must not close the open score) now has to
  hold for readers outside the shell too.
- **`normalized-score-model`** — gains a spelled-name accessor. No change to
  the model's shape, its invariants, or what the importer produces.

## Impact

- **`src/platform/score`** — one new header for the holder, one for the spelled
  name. Both JUCE-free, both unit tested. Neither touches `Score`'s shape.
- **`src/application/tools`** — `ToolServices` gains a field. Every factory in
  `BuiltInTools.cpp` keeps its signature; only the new tool reads the new field.
- **`src/application/shell`** — `ScoreImportState` publishes into the holder
  instead of holding the pointer itself, and the two construction sites of
  `ToolServices` pass it. No new window, no new menu, no new thread.
- **New tool in the roster** — a fourth entry in `builtInToolCatalog()` and a
  fourth factory. By design that is the whole of the shell's involvement; if
  adding this tool requires a shell change, the tool contract is wrong and that
  is itself the finding.
- **Declaration order in `MainComponent`** — `scoreImport` is already declared
  before `liveTools` precisely so that a tool may borrow it. This change is the
  first thing to rely on that, so the comment stops describing a hypothetical.
- **`tools/cmake/Sources.cmake`** — new sources on both the app and test lists.
- **Behaviour change for existing users** — a new tool appears in the Tools
  menu. Nothing existing changes.
- **Downstream** — #32 and #33 read through the seam this establishes; #25–#29
  depend on the answer to "is a tool that knows a target note the same kind of
  tool as one that does not"; #39 takes ownership of the holder from the shell.
