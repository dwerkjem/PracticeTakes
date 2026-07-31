## Why

Stage 3 (#13) turns Practice Takes from a set of live-audio analysis tools into a
practice application: load notation, see the target notes, sing along with
accompaniment. Every issue in that stage — the engraved renderer (#32), the score
tool and target-note overlay (#33), MIDI import (#34), and the session file
(#39) — reads from a score. None of them can start until something owns what a
score *is*.

Today the repository has nothing: no notation code, no MIDI code, no score types.
A search of `src/` for `musicxml`, `staff`, `measure`, or `notation` returns only
a feedback-form category string, `"Notation/MIDI problem"`. The only structured
music data in the application is a fractional MIDI-note value produced by
`PitchDetector`.

That vacuum is the risk this change addresses. If the renderer (#32) is built
first, the score model becomes whatever the renderer's paint routine happened to
need, and MIDI import, playback, and sessions inherit a model shaped by a
drawing loop. Issue #31's third acceptance criterion — "the normalized model is
independent of the rendering widget" — is exactly this concern, and it is only
achievable if the model lands before the widget does.

MusicXML is also unusually hostile to being parsed casually. Duration is
expressed in per-part `<divisions>` units that may change mid-score; voices are
navigated with `<backup>` and `<forward>` cursor moves rather than being listed
in order; ties (sounding) and slurs (visual) are different elements that look
alike; a pickup bar is a `<measure implicit="yes">` whose content does not fill
its own time signature. Real files from Finale, Sibelius, MuseScore, and
Dorico each exercise different corners of the format. Deciding what subset is
supported, and what happens to everything else, is a specification problem
before it is a coding problem.

## What Changes

- Define a normalized, engraving-independent score model: parts, staves,
  measures, voices, notes, chords, rests, ties, pitches with retained spelling,
  clef/key/time changes, tempo and dynamic directions, and lyric syllables — with
  stated invariants and an immutable-after-import ownership rule.
- Define a single score-wide musical time base so every event has an absolute
  musical position independent of each part's `<divisions>`, plus a tempo map
  that converts musical position to seconds.
- Add a MusicXML importer that accepts `.musicxml`, `.xml`, and compressed
  `.mxl`, resolves the `.mxl` container to its root score document, and produces
  either a score or a structured failure.
- Define the supported MusicXML subset for the MVP explicitly, in a document
  that ships with the change, and specify the behaviour for everything outside
  it: unsupported-but-recognised constructs are dropped with a diagnostic;
  unrecognised elements are counted and reported; neither aborts the import.
- Return diagnostics that name a musically meaningful location (part, measure
  number as printed, voice, and event index) and a reason, using the
  repository's existing status-enum plus result-struct error convention.
- Reject malformed, oversized, and structurally invalid input with a specific
  message rather than a generic failure, and never partially apply a failed
  import.
- Run import off the message thread and hand the finished score to the
  application as a shared immutable value, so no consumer needs a lock and no
  consumer can mutate a score another consumer is reading.
- Add unit tests over the model invariants, the time base, the subset boundary,
  and the malformed-input paths, plus import tests for representative vocal and
  piano scores.

Deliberately **not** in scope, because each is a separate issue in the same
family:

- Any drawing, engraving, layout, spacing, or glyph work (#32).
- Score navigation, zoom, part selection, cursors, and target-note overlay
  (#33).
- MIDI file import and the MIDI timeline model (#34). This change may extract a
  shared time/tempo utility, but it does not implement MIDI parsing.
- Playback, transport, synthesis, or anything that reaches the audio thread
  (#35–#38). This change specifies the audio-thread boundary the score model
  must respect; it does not cross it.
- The session/project file (#39). This change adds no persistence format; a
  score is imported from its source file each time.
- Editing, transposition, part extraction, or export of any kind.

## Capabilities

### New Capabilities

- `normalized-score-model`: an application-owned, immutable, engraving-independent
  representation of a score — its entities, its invariants, its single musical
  time base, and the ownership and threading rules that let rendering, playback,
  and session code share one instance safely.
- `musicxml-import`: acceptance of `.musicxml`, `.xml`, and `.mxl` input, the
  documented supported subset, structural validation, and diagnostics that
  identify where and why content was rejected or dropped.

### Modified Capabilities

None. No published capability under `openspec/specs/` describes notation, score
data, or file import, and this change alters no existing behaviour.

## Impact

- **New source tree** for the score model and the MusicXML importer. Its
  location — `src/services/` versus `src/features/` — is an open decision, see
  `design.md` decision 1, because #32, #33, #34, and #39 will all depend on
  wherever it lands.
- **`CMakeLists.txt`** — every source file is listed explicitly, so each new
  `.h`/`.cpp` is added to `target_sources(PracticeTakes ...)`, and each
  unit-testable `.cpp` is also added to `add_executable(PracticeTakesTests ...)`.
  The test target currently links `juce_data_structures`, `juce_dsp`,
  `juce_graphics`, and `juce_gui_basics`; the model and importer must be
  buildable against that set, which means no `JuceHeader.h` and no
  `juce_gui_extra` in model or importer headers.
- **New JUCE surface** — `juce::ZipFile` is used nowhere in the repository
  today and would be introduced here for `.mxl`. XML parsing exists only for
  `AudioDeviceManager` state round-tripping via `juce::parseXML`.
- **`tests/`** — the test directory is flat, has no fixture or resource
  directory, and existing tests that need files create them at runtime in the
  temp directory. Importing "representative vocal and piano scores" needs real
  files, which is a new convention and a licensing question; see `design.md`
  decision 6.
- **Documentation** — `docs/development/ARCHITECTURE.md` gains a score-model
  section covering ownership and the audio-thread boundary, and the change adds
  a supported-MusicXML-subset document that both the renderer (#32) and user
  documentation can point at.
- **No behaviour change for existing users.** Nothing in this change is
  reachable from the current UI; there is no file-open command yet. The
  importer is exercised only by tests until #32 gives it a place to display.
- **Downstream** — #32 and #33 read this model; #34 either shares part of it or
  deliberately does not (decision 4); #39 stores a reference to the source file
  and the selections made against this model.
