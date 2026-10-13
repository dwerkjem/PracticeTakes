# Notation engraving

How a `score::Score` becomes a staff on screen, what is drawn, and what is not.

The score model holds no engraving data at all — invariant 10 is enforced by the
types, so there is nowhere in `Score`, `Part`, `Measure`, `Voice` or
`ScoreEvent` to put a coordinate, a stem direction or a beam group. Computing
all of it is this layer's job.

## The two halves, and why they are split

| | |
|---|---|
| `src/features/score/engraving/` | **JUCE-free.** Decides *where* every glyph goes. Unit tested without a display. |
| `src/features/score/ScoreGlyphPainter.{h,cpp}` | Decides what each glyph *looks like*. `juce::Path` primitives. |
| `src/features/score/ScoreViewComponent.{h,cpp}` | The tool: polls the current score, scrolls, zooms. |

The split is the same one `WorkspaceLayoutState` and `TargetNoteSelection`
already follow, and it exists because the interesting questions are all in the
first row. *Which line is this note on. Is an accidental drawn here. How wide is
this bar. Where does the system break.* A renderer that computed those while
painting would need a `juce::Graphics` to be tested at all, and the only way to
check any of it would be to look at a picture.

So `layOutScore` returns a **value** — `ScoreLayout`, a list of systems, each a
list of glyphs with positions — and `ScoreGlyphPainter` walks it. It is also
deterministic, by test: the same score at the same width produces identical
numbers.

## Coordinates

**Everything is in staff spaces**: the distance between two adjacent staff
lines, which is the unit engravers measure in and the unit a music font is
designed in. Nothing in the layout is a pixel. The tool multiplies by one scale
factor to draw, and that factor is the whole of zoom.

A staff **position** is an integer count of *half* spaces measured downwards
from the top line, because one diatonic step is exactly one half space:

```
 0   top line           F5 in treble      A3 in bass
 2   fourth line        D5                F3
 4   middle line        B4                D3
 6   second line        G4                B2
 8   bottom line        E4                G2
10   one ledger line below                middle C in treble
-2   one ledger line above                middle C in bass
```

Negative is above the staff. Integers rather than doubles, so "on a line" is a
parity test and a test assertion reads as a position you can count off a staff.

## The layout pass

`layOutScore(score, options)` in `ScoreLayoutEngine.cpp`:

1. **Walk the part**, tracking the clef, key and time in force, and placing each
   measure's events in measure-local coordinates. Vertical positions and
   accidentals are decided here, because the accidental rule runs in reading
   order across every voice of the staff.
2. **Pack measures into systems** at `options.availableWidth`. Clef and key
   repeat at every system start; a time signature does not. At least one measure
   per system, however wide it is.
3. **Emit glyphs** with absolute x, recording where every notehead landed.
4. **Resolve ties**, which need both ends placed before either can be drawn.

### The pieces each rule lives in

| File | The rule it owns |
|---|---|
| `StaffGeometry.h` | The units, and the conversions between half spaces, lines and staff spaces. |
| `StaffPosition.{h,cpp}` | A clef plus a spelled pitch gives a position, ledger lines and a stem direction. |
| `NoteValue.{h,cpp}` | Ticks become a notehead, a flag count and a dot count, under a tuplet ratio. |
| `KeySignatureLayout.{h,cpp}` | Where a key signature's accidentals sit, per clef. |
| `MeasureAccidentals.{h,cpp}` | Which notes need an accidental drawn. |
| `Accidental.h` | The accidental signs, shared by the two above. |
| `ScoreLayout.h` | The glyph types, the system, and the spacing options. |

### A clef is a reference pitch on a line

The model keeps a clef as MusicXML does — a sign, the line it centres on, and an
octave change — which is more general than an enum of named clefs and makes
positioning one expression rather than a table. The sign names a pitch (G clef →
G4, F clef → F3, C clef → C4); every other pitch is a number of diatonic steps
from it.

`clef.octaveChange` is **undone, not applied**. A vocal tenor clef is a G clef
with an octave change of −1, and MusicXML's `<pitch>` under it is the pitch that
*sounds* — an octave below where it reads. The renderer draws where it reads.
Getting this backwards puts a whole part an octave out with nothing failing.

A percussion, tablature or `none` clef names no pitch. Such a staff is **not
drawn**, and the layout says why.

### Key signature positions are a table, not a rule

Where a key signature's accidentals sit is convention. The octave each one is
written in is chosen to keep the group on the staff, and no rule that holds for
one clef holds for all of them: treble F-sharp is the top line (F5), bass
F-sharp is the fourth line (F3).

So there is one hand-checked table for a G clef on line 2, plus one half space
for a C clef on line 3 and two for an F clef on line 4. Those three reproduce
the engraved positions for treble, alto and bass exactly, for all seven sharps
and all seven flats, and `KeySignatureLayoutTests` pins every one of them.

A clef of a known sign on a *different* line keeps the pitches and shifts with
the staff; `keySignatureIsConventional` is false for it and the layout emits a
notice, because such a placement has not been checked against an engraved
reference.

### Accidentals are a per-measure state machine

An accidental is drawn when a note's alteration differs from the one **in
force** for that step in that octave — set by the key signature at the start of
each bar, then overridden by whatever the bar has already said. A natural sign is
what cancelling looks like.

The key signature applies in every octave. An accidental applies only in its
own. A note tied *from* another draws nothing, because the accidental belongs to
the note that was struck.

`MeasureAccidentals` is constructed per measure, so "reset at the barline" is a
lifetime rather than a method somebody has to remember to call.

### Spacing

Proportional to duration, with a floor: `durationStretchPerQuarter` staff spaces
per quarter note, and `minimumSlotWidth` between successive onsets so a run of
sixteenths still has room for a notehead. A measure is as wide as its *musical
length*, so a bar of 4/4 holding one whole rest is still a bar of 4/4 wide — and
that rest is centred in it.

Real engraving spaces notes non-linearly and resolves collisions. Not doing that
is deliberate: proportional spacing is predictable, is one line of arithmetic,
and is right often enough to read. Replacing it later is one function.

### Stems

With one voice, by pitch: up below the middle line, down on it and above. With
two or more voices on the staff, by voice — voice 1 up, the rest down, whatever
the pitch — which is the convention and what makes two voices on one staff
readable.

## What is drawn

Staff lines, clefs (G, F and C, with an octave-change marker), key signatures,
time signatures, noteheads from a breve to a 128th, chords, accidentals
including doubles, augmentation dots, stems, flags, ledger lines, rests, ties,
barlines including repeats and the final double bar, lyrics with hyphens, tempo
marks, dynamics, and printed bar numbers at system starts.

## What is not drawn, and why

Every one of these produces a sentence in `ScoreLayout::notices`, which the tool
shows. That list is the reason a first pass is an acceptable thing to ship:
#200's acceptance criteria state the rule in the general case — content must be
*"either rendered correctly or reported, never silently misplaced."*

| Not drawn | Why |
|---|---|
| **Beams** | Eighths and shorter get flags, which is correct notation and not idiomatic for a run of them. Beaming needs a grouping pass over the time signature. |
| **More than one part** | The first part is drawn and the rest are named. Part selection is #33. |
| **More than one staff of a part** | The first staff is drawn, and events on the others are counted. The braced grand staff is #200, which this change unblocks. |
| **Page presentation** | Continuous vertical scroll only. Navigation is #33. |
| **Volta brackets, D.C./D.S. jumps** | `Measure::repeats` carries them uninterpreted. Repeat barlines *are* drawn. |
| **A tie across a system break** | Both ends are placed before either is drawn; when they land on different systems the tie is reported. |
| **Cancelling naturals at a key change** | A key change to no accidentals draws an empty signature. |
| **Slurs, articulations, ornaments, hairpins, fingering** | Mostly not in the model, by invariant 10 or by the supported subset. |
| **A SMuFL music font** | Bravura would look far better and brings a licence, a binary asset and a packaging question. The glyphs are primitives, behind one file, and the layout already works in the unit a music font is designed in. |

## Verifying it

The layout is covered by `src/tests/features/score/engraving/`. What it *looks
like* is Component code and only the capture harness ever sees it, through two
approved states — `score-view-notation` and `score-view-notation-large` — and
`src/tests/resources/harness-scores/notation-sample.musicxml`, a fixture written
so that every construct the renderer draws appears in it once.

Those are the first approved states that open a score **into a tool**. That was
impossible until `ApprovedWindowState::showScoreImportSummary` arrived with this
change: the only way to get a score into the running application is
`startScoreImport`, which also opens the summary window over whatever the
surface is about. See finding 7 in
[`score-model-findings.md`](score-model-findings.md).
