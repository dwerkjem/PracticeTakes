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
| `src/features/score/ScoreGlyphPainter.{h,cpp}` | Decides what each glyph *looks like*: Bravura glyphs, and lines at Bravura's weights. |
| `src/features/score/MusicFont.{h,cpp}`, `SmuflGlyphs.{h,cpp}` | The embedded music font, as outlines in staff spaces, and which SMuFL glyph draws what. |
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
| `BeamGrouping.{h,cpp}` | Which notes beam together, from the time signature. |
| `BeamGeometry.{h,cpp}` | Where a group's beams go, and how long each stem under them is. |
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

A beam joins stems pointing the same way, so a **beamed group overrules both**
with one direction of its own, chosen by the note furthest from the middle line.
A tie goes down, as an engraver's does. In a multi-voice measure the voice rule
already gives every group one direction, because a group never spans two voices.

### Beaming

Beam grouping is a metrical decision before it is a visual one. Beaming that
ignores the time signature does not merely look unidiomatic — it tells a reader
the wrong metre, so six eighths beamed 3+3 read as 6/8 whatever the signature
says. `BeamGrouping` therefore derives the group from the metre alone:

| Metre | One group |
|---|---|
| Simple — 2/4, 3/4, 4/4, 2/2, 6/4 | One beat: a quarter in x/4, a half in x/2. |
| Compound — 3/8, 6/8, 9/8, 12/8, 6/16 | One dotted beat: three of the notated unit. |
| Unreadable — a zero or negative beats or beat type | Nothing. Beaming everything together would assert a metre, and the metre is what is in doubt. |

Compound means the *beat* divides into three, which is `beatType >= 8` and
`beats` divisible by three. 6/4 is six quarter beats, not two dotted ones, and
is simple by that rule; 3/8 is one group of three, which is what an engraver
writes.

A run also breaks at anything that is not beamed — a rest, a grace note, a
quarter note or longer — and at a gap in time, so two eighths either side of a
silence are not a group however adjacent their indices are. A run of one is a
flag, not a beam of its own.

**MusicXML's own `<beam>` elements are not an input.** The importer reads and
drops them, because the model refuses to hold a beam group by invariant 10, for
the same reason it refuses to hold a stem direction. So the grouping is derived,
and the layout says so in a notice rather than letting a reader assume the
file's own beaming was honoured.

`BeamGeometry` then places the bars. The beam is a straight line through the two
outer stems' natural tips, with its rise clamped to `maximumBeamRise` and the
whole line then pushed away from the staff by however much it takes to leave
every stem at least `minimumStemLength` long and clear of its chord's far
notehead. **Pushing the line rather than lengthening one stem is what keeps it
straight**, and a beam that is not straight is not a beam.

Level 1 is the beam nearest the noteheads, which every beamed note carries;
each further level stacks one thickness and one gap further away, and each stem
reaches the outermost level that meets it. A level whose run is a single note
is drawn as a **partial beam** — a stub pointing back towards the beat, or
forward when there is nothing behind it — and never more than halfway to its
neighbour, or two facing stubs would touch and read as a bar.

The painter draws every beam, full or partial, as one filled parallelogram with
vertical ends. Vertical rather than square to the slope, so the stems meeting it
do so flush; its ends reach half a stem's width past the stems they join, along
the beam rather than straight out from it.

## What is drawn

Staff lines, clefs (G, F and C, with an octave-change marker), key signatures,
time signatures, noteheads from a breve to a 128th, chords, accidentals
including doubles, augmentation dots, stems, flags, beams with their partial
beams, ledger lines, rests, ties,
barlines including repeats and the final double bar, lyrics with hyphens, tempo
marks, dynamics, and printed bar numbers at system starts.

## What is not drawn, and why

Every one of these produces a sentence in `ScoreLayout::notices`, which the tool
shows. That list is the reason a first pass is an acceptable thing to ship:
#200's acceptance criteria state the rule in the general case — content must be
*"either rendered correctly or reported, never silently misplaced."*

| Not drawn | Why |
|---|---|
| **A beam over a rest** | An engraver beams a beat holding a short rest; a rest breaks the run here. Reported when it happens. |
| **Beamed grace notes** | A run of grace notes is conventionally beamed and is drawn with flags. Reported when it happens. |
| **Beams across a barline or a system break** | A group is resolved per measure, so neither can arise. The cross-staff diagnostic stands. |
| **Tuplet brackets and numbers** | A tuplet's notes beam correctly, because the arithmetic works out; nothing names the ratio on the page. |
| **More than one part** | The first part is drawn and the rest are named. Part selection is #33. |
| **More than one staff of a part** | The first staff is drawn, and events on the others are counted. The braced grand staff is #200, which this change unblocks. |
| **Page presentation** | Continuous vertical scroll only. Navigation is #33. |
| **Volta brackets, D.C./D.S. jumps** | `Measure::repeats` carries them uninterpreted. Repeat barlines *are* drawn. |
| **A tie across a system break** | Both ends are placed before either is drawn; when they land on different systems the tie is reported. |
| **Cancelling naturals at a key change** | A key change to no accidentals draws an empty signature. |
| **Slurs, articulations, ornaments, hairpins, fingering** | Mostly not in the model, by invariant 10 or by the supported subset. |

## The music font

Notation is drawn in **Bravura**, the SMuFL reference font, embedded as
described in [`tools/packaging/fonts/README.md`](../../../tools/packaging/fonts/README.md).

SMuFL fixes one em at four staff spaces. `MusicFont` converts every outline it
uses into staff spaces once, at load, so the painter never sees a font size and
zoom is still one number. Each glyph keeps SMuFL's origin, which is the layout's
own anchor: a notehead's on its staff position at its left edge, a clef's on the
line it names, a rest's on the line it is placed against. Placing a glyph is one
translation.

What SMuFL draws as **lines rather than glyphs** stays that way: staff lines,
ledger lines, stems, beams and barlines. Each is as long as the layout says,
which no fixed glyph can be, and each is drawn at Bravura's published thickness.

Two layout numbers moved to match the font: `noteheadWidth` is Bravura's 1.18,
and `stemThickness` is a layout option, so a stem's centre line is set in by
half its thickness and its outer edge sits flush with the notehead. Beam ends
reach the outer edges of their stems in the layout rather than in the painter.
`MusicFontTests` checks the notehead width against the font.

Some placement rules are the painter's, not the font's. An accidental is
right-aligned before its notehead, and a ledger line is shortened on that side,
as Gould prescribes, so the sign does not sit on the line's end. A rest is
centred on the layout's x. A time signature's two numbers are centred on each
other.

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
