## Context

`src/platform/score/` holds a complete, immutable, engraving-independent score
model, and `CurrentScore` already publishes the open score to tools through
`ToolServices`. What does not exist is anything that draws it. The Target Note
tool (#187) proved the model can be consumed and produced a written list of what
consuming it revealed — `docs/development/architecture/score-model-findings.md`.
This change is the second consumer, and it is the one that finding list was
written for: findings 3, 4 and 6 are all things a renderer needs and a
one-note tool could get away without.

Constraints inherited from the repository, none of which this change may bend:

- **Invariant 10 stands.** The model holds no coordinates. Everything positional
  this change computes lives outside `Score` and is derived, never stored back.
- **The audio thread never touches a score** (`Score.h`), so nothing here is
  real-time code. Layout runs on the message thread.
- **A `src/features/*` tool reaches application state only through
  `ToolServices`**, never back into the shell.
- **Non-trivial logic is split out of `Component` classes** so it can be tested
  without a display — the `WorkspaceLayoutState` pattern. Engraving is the
  largest body of non-trivial logic the application has yet had, so this matters
  more here than anywhere it has been applied so far.

Issue #200 ("grand staff pairing, brace, per-hand staff assignment") is folded
in rather than sequenced after, because its acceptance criteria — vertical
alignment of equal ticks, independent per-staff clefs — are properties of the
layout model itself. Building a single-staff renderer first and pairing staves
afterwards would mean designing the column model twice, the second time as a
retrofit.

## Goals / Non-Goals

**Goals:**

- An imported score is readable on screen as staff notation.
- A two-staff piano part draws as one braced grand staff, with the same tick at
  the same x on both staves and per-staff clefs that change independently.
- The whole of layout is unit-testable with no display and no JUCE.
- Notation this pass cannot draw is *reported*, never silently misplaced.
- The score-model gaps the renderer hits are closed in the model, once, rather
  than worked around in the renderer.

**Non-Goals:**

- Beams that cross the brace. Detected and reported, not drawn.
- Slurs, ties as drawn curves (tie *linkage* is modelled; drawing the arc is
  not in this pass), ornaments, articulations beyond what a notehead carries.
- Page layout, printing, and pagination. The surface scrolls vertically.
- A playback cursor or any navigation (#33), editing, and MIDI-to-score
  evaluation (#202, #203).
- Optimal (Gourlay-style) spacing with line-breaking optimisation. See
  decision 3.

## Decisions

### 1. Layout is a JUCE-free value tree in staff spaces; the Component only paints

`src/platform/score/engraving/` takes a `const Score&` and a width, and returns
a laid-out result: systems, each holding staves, each holding placed glyphs with
positions. Positions are in **staff spaces** (`sp`, the distance between two
staff lines) — the unit real engraving uses — with the pixel scale applied only
at paint time.

`src/features/score/notation/` holds the `ToolComponent`, which calls the layer,
caches the result, and paints it. It makes no layout decision whatsoever.

*Why:* every acceptance criterion on #200 is a statement about positions —
"simultaneous events align vertically", "a clef change on one staff does not
disturb the other". If layout lives inside a `paint()` routine, each of those is
verifiable only by a human looking at a screenshot, and `Component` code sits
outside `PracticeTakesTests` by design. As a value tree they are assertions
about numbers. This is `WorkspaceLayoutState`'s split applied to the biggest
piece of logic in the codebase.

*Alternative considered:* draw directly in `paint()` with a running cursor, the
way most small notation viewers are written. Rejected: it is faster to a first
picture and leaves the entire feature permanently unverifiable.

*Alternative considered:* a third-party engraving library (Verovio, VexFlow).
Rejected: Verovio consumes MusicXML directly and would make the normalized model
dead weight for its own renderer, pulls in a large dependency with its own font
and layout policy, and the repository has no C++ dependency of that size;
VexFlow is JavaScript.

### 2. Glyphs come from Bravura, bundled the way IBM Plex already is

Notation needs real music glyphs: clefs, accidentals, rests, noteheads, flags.
**Bravura** is the reference font for SMuFL (the Standard Music Font Layout), it
is SIL OFL 1.1 — the same licence class as the IBM Plex faces already in
`tools/packaging/fonts/` — and it is added to the existing
`juce_add_binary_data(PracticeTakesFonts ...)` target, so the mechanism is one
the build already has and the glyphs ship embedded rather than looked up by
name. The reason the existing comment gives for embedding IBM Plex applies
doubly here: a name-based lookup would fall back to the platform sans and the
symptom would be a page of tofu.

**The layout layer does not depend on the font.** It depends on *metrics* —
advance widths and SMuFL anchor points — carried as a small JUCE-free table in
`engraving/`. That is what keeps layout testable without a display, and it is
the only reason the split in decision 1 is achievable at all.

*Alternative considered:* hand-drawn `juce::Path` glyphs, no new asset. Viable
for noteheads, stems, ledger lines, barlines and the brace; not viable for
clefs, accidentals, rests and flags without hand-authoring bezier data that
would be worse than the font in every respect and unmaintainable.

*Alternative considered:* Petaluma or Leland (also SMuFL, also OFL). Both are
fine; Bravura is the reference implementation, so its metadata is the one SMuFL
documents against.

### 3. Horizontal spacing is column-based across the whole system

For each system, collect every distinct absolute tick at which *any* staff of
*any* part in that system has an event. Each distinct tick is a **column**.
Every event sits in the column of its own absolute onset. A column's width is
the larger of:

- a spacing function of the musical duration until the next column — a
  neo-Gourlay power law (`width ∝ duration^k`, `k ≈ 0.6`), which is why a half
  note gets more room than a quarter but not twice as much; and
- the widest glyph group in that column, plus its minimum separation.

*Why this and not something simpler:* **vertical alignment is the entire point
of #200**, and in a column model it is not a feature — it is the data structure.
"The same tick sits at the same x on both staves" is true by construction
because both staves index the same column list. Any model that lays out each
staff independently and then tries to reconcile them is solving alignment
repeatedly and approximately.

*Alternative considered:* width strictly proportional to duration. Simplest, and
it is what makes amateur renderers look wrong — a bar of whole notes sprawls and
a bar of sixteenths is unreadable. The power law is barely more code.

*Alternative considered:* full Gourlay optimal spacing with global line-break
optimisation. Correct and what Finale/Dorico do; far more than a first pass
needs. System breaking here is greedy: fill the available width, break, repeat.

### 4. A grand staff is a part with `staffCount > 1`, and nothing more special

The model already carries everything needed: `Part::staffCount` is 2 for a
piano, `ScoreEvent::staff` says which staff each event sits on, and
`AttributeChanges::clefs` is per staff with "one entry per staff that changes
clef". So grand staff support is not a new concept in the model — it is the
renderer finally honouring fields that were there from the start.

- A part contributes `staffCount` staves to every system.
- Staves of the same part are joined by a **brace**; separate parts by a
  bracket or nothing. The brace is drawn for `staffCount > 1`, so it appears for
  a piano and not for a vocal line, with no per-instrument table.
- **Clef state is per staff.** Each staff of each part carries its own running
  clef, advanced by the `Clef` entries in `AttributeChanges::clefs` that name
  *that* staff number. A bass staff changing to treble mid-score updates one
  running state and cannot reach the other — which is acceptance criterion 3,
  satisfied structurally rather than by care.
- Key and time signature are per *part* and drawn on every staff of it, which
  is what the model says (`AttributeChanges::key`/`time` are not per staff).

### 5. Cross-staff content is placed by what the file says, and its beams are reported

MusicXML writes cross-staff notation as a note whose `staff` differs from the
prevailing staff of its voice. The model preserves this per event, so **placing
such a note is already correct**: it renders on the staff it names. What this
pass does not draw is the beam connecting it back across the brace.

So the rule is: place every event on `ScoreEvent::staff`, always; detect a voice
whose events change staff within a measure; and emit a render diagnostic naming
the measure — "bar 12: beam crosses staves, drawn unbeamed". Nothing is
misplaced, and the limitation is stated rather than left for the user to notice.
This is what satisfies #200's fifth acceptance criterion without drawing a
cross-staff beam.

### 6. Render diagnostics reuse `score::Diagnostic` and surface in the tool

Everything the renderer cannot draw goes into a diagnostic list alongside the
layout, in the same shape the importer already uses. The tool shows a count with
a way to read them. A renderer that silently omits what it does not understand
is the failure mode #200 explicitly rules out, and a separate diagnostic type
would mean two things to read and two to display.

### 7. Model additions go in the model, now

The renderer needs three things the findings document already predicted:

- **`score::absoluteOnset(measure, event)`** (finding 4) — the renderer computes
  absolute ticks for every event in the score, which is exactly the sum a caller
  is documented to get silently wrong outside bar 1.
- **Event traversal in `score::`** (finding 3) — "visit every event of a part in
  musical order" is the renderer's inner loop and would otherwise be the second
  hand-written four-level nested loop in the application, after the Target Note
  tool's.
- **A stated ordering for a chord's notes** (finding 6) — the renderer stacks
  noteheads and must know whether `notes[0]` is the lowest. It is **not**: the
  importer appends each chord tone in the order the file wrote it
  (`MusicXmlMeasureReader.cpp`, the `isChordTone` branch — `appendNote` onto the
  cursor's event, no sort anywhere). So the guarantee to write down is *source
  order*, and the renderer sorts by written pitch itself for stacking. Stating
  the weaker truth is the point: a consumer that assumed ascending pitch would
  be right on most files and wrong on the ones whose exporter differs.

These are additions to `normalized-score-model`'s stated guarantees, not to what
the model stores. Invariant 10 is untouched.

### 8. The surface is a tool, `score-view`

Registered through `BuiltInToolCatalog.h` and `BuiltInTools.cpp` like every
other tool, reading `ToolServices::currentScore`, polling on its own timer and
comparing the `shared_ptr` against the one it last laid out — the change check
finding 2 of the findings document confirmed is sufficient. It persists no
settings in this pass, so it declares no `settingsVersion`.

Layout is recomputed when the score pointer changes or the width changes, and
cached otherwise; it is O(events) and runs on the message thread, where a score
of a few thousand events is not a frame-rate concern. If it becomes one, the
cache is already the seam to fix it behind.

## Risks / Trade-offs

- **Engraving is a deep field and "readable" is a judgement, not a predicate.**
  → Scope is pinned to #200's five acceptance criteria plus "a pianist can read
  it". Anything beyond — beaming aesthetics, collision avoidance, optimal
  spacing — is explicitly a later pass, and the diagnostic channel (decision 6)
  is how the gap stays visible instead of silent.

- **The `Component` that paints this is outside `PracticeTakesTests`, like every
  other tool component.** → Decision 1 is the mitigation: the paint routine is
  reduced to "draw these glyphs at these positions", so the untested surface is
  as thin as it can be made. The golden-image suite covers the rest, which means
  the four verification edits in `adding-a-tool.md § Make it verifiable` are not
  optional here.

- **Bundling Bravura adds roughly 700 KB to the binary and a new third-party
  asset.** → Same mechanism, same licence class, and the same justification as
  the IBM Plex faces already embedded. Recorded here so it is a reviewed
  decision rather than a surprise in a diff.

- **Column spacing across a whole system is O(distinct ticks) per system and
  allocates.** → Message thread only; the audio thread is forbidden from
  touching a score at all, so the usual constraint does not apply. Cached on
  score pointer and width.

- **A score whose parts disagree about measure count would break the column
  model.** → Invariant 1 already requires every part to start measure *n* at the
  same tick, and `ScoreInvariants` enforces it at import. The renderer may rely
  on it, and should assert rather than defend.

## Open Questions

- ~~**What order does the importer actually emit a chord's notes in?**~~
  **Resolved: source order.** Read off `MusicXmlMeasureReader.cpp`; see
  decision 7. Still needs a corpus test pinning it before the spec guarantee is
  relied on.
- **A two-staff fixture with a mid-score clef change.**
  `src/tests/resources/harness-scores/clean-import.musicxml` has a `<staves>2</staves>`
  piano part with per-staff clefs, which covers the basic grand staff. The
  `.mxl` corpus is 37 art songs — voice plus piano — so grand staff material is
  plentiful there, but it is zipped and was not searchable for a *mid-score*
  clef change. If none exists, a minimal fixture is authored for acceptance
  criterion 3.
- **Obtaining Bravura requires a download** (github.com/steinbergmedia/bravura,
  OFL-1.1). The font file and its `bravura_metadata.json` have to be fetched and
  committed before the glyph work can start; the layout work in decision 1 does
  not block on it, only on the metrics table it feeds.
