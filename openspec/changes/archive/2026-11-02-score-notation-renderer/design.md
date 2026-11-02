## Context

The score model is finished and unused for drawing. What it offers a renderer,
and what it deliberately withholds, was recorded on #32 by the developer when
#31 landed and is the starting point here:

1. **The model holds no engraving data, by construction.** There is nowhere in
   `Score`, `Part`, `Measure`, `Voice` or `ScoreEvent` to put a coordinate, a
   font, a page or system break, a stem direction, or a beam group. All of it is
   this change's to compute.
2. **`Pitch` keeps its spelling *and* its sounding number**, kept consistent by
   invariant 5. The spelling picks the staff line and the accidental; the number
   cannot, because C-sharp and D-flat share it.
3. **Musical time is integer ticks, 3840 per quarter note, fixed score-wide.**
   Spacing arithmetic is integer comparison, not rational and not floating
   point.
4. **The model is as-written, and a chord is one event.** Measures appear once,
   in source order; nothing expands a repeat. A chord is a single `ScoreEvent`
   carrying several pitches.

Two gaps were flagged there and both hold: `<part-group>` bracketing is not
imported (so which parts brace together is unavailable — #200's problem, not
this change's, since this change draws one staff), and transposing instruments
are imported at written pitch, which is what a renderer wants.

`score-model-findings.md` adds one more that this change has to deal with
directly: **finding 7 — the harness cannot get a score into a tool.** For a
text tool that was an inconvenience. For a renderer it is fatal: the entire
deliverable is a picture.

## Goals / Non-Goals

**Goals**

- A supported MusicXML file renders as readable notation, from the model alone.
- Every engraving rule worth asserting is in a JUCE-free file with a unit test.
- The layout is deterministic — same score, same width, same numbers — so it can
  be snapshot tested without a display.
- Nothing is silently misplaced. What is not drawn is reported in the layout and
  shown to the user.

**Non-Goals**

- Engraving quality comparable to a notation editor. No font, no optical
  spacing, no collision avoidance beyond what the spacing model gives.
- Beaming, multi-staff, multi-part, pages. See *Out of scope* in `proposal.md`.
- Any coordinate in the score model. Invariant 10 is not negotiable here.

## Decisions

### 1. Coordinates are staff spaces, and the layout is a value

`ScoreLayout` holds doubles in **staff spaces** — the distance between two
adjacent staff lines, the unit engravers measure in — with the origin at the top
line of each system's staff and y increasing downwards. The component multiplies
by one scale factor to get pixels, and that factor is the whole of zoom.

The alternative was laying out in pixels at paint time, which is how a smaller
renderer would be written. Rejected because it makes the layout untestable: a
test would need a `juce::Graphics`, and the thing most worth pinning — *which
line is this note on* — would be checked by looking at a picture. Staff spaces
also make the test assertions readable: a note on the top line is at 0, the
bottom line is at 4, and middle C in bass clef is at -1 staff spaces, which is
a sentence a reader can check against a staff.

Staff **positions** are integers in *half* spaces, counted down from the top
line, because one diatonic step is exactly one half space. `staffSteps = 0` is
the top line, 4 is the middle line, 8 is the bottom line. Integers here rather
than doubles so that "on a line" and "in a space" is parity rather than a
floating-point comparison.

### 2. A clef is a reference pitch on a line, so positioning is arithmetic

The model keeps a clef as MusicXML does: a sign, the line it centres on, and an
octave change. That is more general than an enum of named clefs, and it makes
the position computation one expression rather than a table: the sign names a
pitch (G clef → G4, F clef → F3, C clef → C4), the line says where that pitch
sits, and every other pitch is a number of diatonic steps away from it.

`clef.octaveChange` is *undone* rather than applied. A vocal tenor clef is a G
clef with `octaveChange == -1`, and MusicXML's `<pitch>` for a note under it is
the pitch that sounds — an octave below where it reads. The renderer draws where
it reads, so the written pitch is shifted back up by the octave change before
positioning. Getting this backwards puts a whole part two octaves out, which is
why it has its own test.

A percussion, TAB or `none` clef names no pitch. Those staves are **not drawn**
and the layout says so, rather than placing every note on the middle line and
hoping.

### 3. Key signature positions: one table, shifted per clef sign

Where the accidentals of a key signature sit is convention, not arithmetic — the
octave each one is written in is chosen to keep the group on the staff, and the
choice is not derivable from a rule that holds for every clef. (Treble F-sharp
is the top line, F5; bass F-sharp is the fourth line, F3; both are "the F that
fits".)

So: one hand-checked table of half-space positions for a G clef on line 2, and a
per-sign offset — C clef on line 3 is that table plus one half space, F clef on
line 4 plus two. Those three reproduce the engraved positions for treble, alto
and bass exactly, for all seven sharps and all seven flats. A clef of one of
those signs on a *different* line shifts by the line difference, which keeps the
pitches right and the group possibly high or low on the staff; the layout emits a
notice when it does that, because a tenor clef signature placed by this rule is
not one anybody checked against an engraved reference.

The alternative considered and rejected was deriving the octave from a rule
("the highest position that is not above the staff"). It reproduces treble and
bass for most accidentals and disagrees on the fifth sharp and the fifth flat,
which is exactly the kind of near-miss that looks right until a reader who reads
music sees it.

### 4. Accidentals are a per-measure state machine, not a per-note decision

An accidental is drawn when a note's alteration differs from what is **currently
in force** for that step in that octave, which is the key signature at the top
of the bar and then whatever the bar has already said. A natural sign is what
"in force and now cancelled" looks like.

This is one small class with a map from step-and-octave to the alteration in
force, reset at each barline. It is in its own file with its own tests because
the failure mode is subtle and cumulative: a missing natural or a repeated sharp
is not a crash, it is a wrong note that a player plays.

A note tied *from* a previous note never draws an accidental — the accidental
belongs to the note that was struck. That covers the common case of a tie across
a barline, where repeating it would be wrong.

### 5. Spacing is proportional with a floor, and deliberately simple

Each event's width is `durationStretch` staff spaces per quarter note, with a
minimum so that a sixteenth is still wide enough for a notehead and an
accidental. A measure is as wide as its content, plus room at the start for
whatever clef, key or time signature appears there. Systems break when the next
measure would not fit the available width.

Real engraving spaces notes non-linearly (a sixteenth is wider than a quarter of
a quarter) and resolves collisions. Not doing that is a deliberate first pass:
proportional spacing is predictable, is one line of arithmetic, and is right
often enough to read. It is also exactly what a later pass can replace without
touching anything else, because the spacing decision is one function.

### 6. Multiple voices: stems by voice, not by pitch

With one voice in a measure, stem direction follows the usual rule — below the
middle line, stem up. With two or more, voice 1 stems up and voice 2 stems down
regardless of pitch, which is the convention and which is what makes two voices
on one staff readable. Voices beyond the second follow voice 2.

### 7. The layout reports what it did not draw

`ScoreLayout::notices` is a list of sentences, and the tool shows them. Beams,
further parts, further staves, voltas, jumps, an unnotatable duration, an
unpitched clef, an unusual clef's key signature — each produces one.

This is the *only* reason a first pass of a renderer is an acceptable thing to
ship. #200's acceptance criteria already say it in the general case: content
must be *"either rendered correctly or reported, never silently misplaced."*
A notice is how this change obeys that rule for everything it does not yet do.

### 8. Suppressing the import summary makes the renderer verifiable

One field on `ApprovedWindowState`: a state that names a score fixture *and*
opens a tool does not show the import summary window. `finishScoreImport`
checks it, so the score is published into `CurrentScore` and the tool picks it
up on its own timer, with nothing overlapping the picture.

This closes finding 7 rather than working around it. The invariant the existing
tests were protecting — one subject per picture — is kept and restated: a state
showing the summary opens nothing else, and a state suppressing it opens exactly
one tool.

## Risks / Trade-offs

- **The glyphs are drawn from primitives, not from a music font.** They will be
  recognisable rather than beautiful; the clefs especially. Mitigated by keeping
  every glyph behind one painter file, so adopting Bravura later is one file's
  worth of change and no layout change at all — the layout already works in
  staff spaces, which is the unit a music font is designed in.
- **Proportional spacing will look wide on sparse music and tight on dense
  music.** Accepted per decision 5; it is one function.
- **Flags instead of beams will read as unidiomatic** to anyone who reads music,
  on anything with a run of eighths. This is the most visible deliberate gap,
  which is why it is the first notice the tool shows.
- **One part only.** On the piano scores the roadmap now leads with, that means
  the right hand and a notice. #200 is the issue for it and is unblocked by this
  change landing, which is the point.
