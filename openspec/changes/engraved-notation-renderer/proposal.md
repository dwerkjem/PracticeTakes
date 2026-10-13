## Why

A score can be imported and the model can be read, but nothing in the
application draws it: the only score surface is the Target Note tool, which
shows *one* note as text and whose own header says engraving belongs elsewhere
(`src/features/score/TargetNoteComponent.h:17`). Until a piano score is
*readable* on screen, practising against it is not possible, and every
downstream capability — a playback cursor, hands-separate practice, note
evaluation — has nothing to anchor to.

This change makes an imported score visible as staff notation, and it treats a
two-staff piano part as a braced grand staff rather than as two unrelated
staves, because the first instrument the application targets is the piano and a
grand staff is the form a pianist reads.

## What Changes

- **A JUCE-free engraving layer** under `src/platform/score/engraving/` that
  turns a `Score` into a laid-out page: systems, staves, measures, and the
  position of every notehead, accidental, stem, clef, key signature and time
  signature, in abstract staff-space units. No JUCE types, so the whole of
  layout is unit-testable without a display — the split the repository already
  applies to `WorkspaceLayoutState`.
- **A thin rendering surface** that paints that layout and nothing else. All
  decisions live in the layer above; the `Component` only draws.
- **A new Score View tool**, registered through `ToolCatalog`/`ToolRegistry`
  like every other tool, reading the open score through `ToolServices` and
  `CurrentScore`. It is the first tool whose primary content is a score.
- **Grand staff pairing**: the staves of one multi-staff part are drawn as one
  braced system, vertically aligned so the same tick sits at the same x on
  every staff of the part, with per-staff clefs that change independently.
- **Cross-staff content is detected and reported, never silently misplaced.**
  Beams crossing the brace are out of scope for this pass; a diagnostic saying
  so is not.
- **Score-model additions the renderer forces**, each already written down as a
  finding in `docs/development/architecture/score-model-findings.md`: event
  traversal in `score::` (finding 3), absolute onset as a function rather than
  a sum the caller remembers (finding 4), and a stated ordering for the notes
  of a chord (finding 6). The renderer is the second consumer; these are the
  things the first consumer predicted it would need.

Not in this pass, and deliberately: slurs, beams across the brace, ornaments,
page layout for printing, a playback cursor (#33), and editing. The score is
drawn, not interacted with.

## Capabilities

### New Capabilities
- `score-notation-rendering`: engraving an imported score as readable staff
  notation — staff and system layout, horizontal spacing against musical time,
  clefs/keys/time signatures, noteheads and accidentals, multi-staff parts
  drawn as a braced grand staff with aligned ticks, and the explicit reporting
  of notation this pass cannot draw.

### Modified Capabilities
- `normalized-score-model`: adds requirements for traversing a score's events
  in musical order, for obtaining an event's absolute onset without the caller
  recomputing it, and for the guaranteed ordering of the notes within a chord.
  These are stated guarantees the model does not currently make, not changes to
  what it stores — invariant 10 and the engraving-independence requirement are
  untouched.

## Impact

- **New code**: `src/platform/score/engraving/` (JUCE-free layout),
  `src/features/score/notation/` (the tool component and its painting), and
  their mirrors under `src/tests/`.
- **Existing code**: `src/platform/score/` gains traversal and absolute-onset
  helpers; `BuiltInToolCatalog.h` and `BuiltInTools.cpp` gain one entry each;
  `tools/cmake/Sources.cmake` gains both source lists.
- **Verification surface**: an approved window state naming the tool,
  `all-tools-docked` updated, the pinned tool count bumped, and a surface in
  `tools/scripts/testing_suite/surfaces.py` — the four edits
  `adding-a-tool.md § Make it verifiable` now documents.
- **Assets**: drawing notation requires music glyphs. Which source, and its
  licence and bundling cost, is settled in `design.md`; it is the one external
  dependency this change may add.
- **Issues**: closes #32, and #200 within it. Does not touch #31's import work.
- **Not affected**: the audio thread and the MIDI callback. Nothing here runs
  outside the message thread, and `Score.h` already forbids the audio thread
  from touching a score at all.
