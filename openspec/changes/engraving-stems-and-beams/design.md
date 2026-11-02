# Design

## Context

The first engraving pass (#32, `score-notation-renderer`) laid out noteheads,
stems, flags, dots, chords and two-voice stem direction. Beaming was the one
piece of #226 it did not do, and it said so in a notice. This change is that
piece.

The layer it goes in is already settled: `src/features/score/engraving/` is
JUCE-free and returns a value, `ScoreGlyphPainter` draws it, and the model holds
no engraving data at all by invariant 10. Nothing here changes any of that.

## Decisions

### 1. Grouping is derived from the metre, not read from the file

MusicXML carries `<beam>` elements per note. The importer reads and drops them,
because `ScoreEvent` has nowhere to put a beam group — invariant 10 refuses it
for the same reason it refuses a stem direction.

That could be read as a gap to close. It is not, for this change: adding beam
groups to the model would put a *presentation* decision in it, and the rule that
keeps the model clean is worth more than one file's beaming. The grouping is
derived instead, and the layout **reports that it was**, so nobody concludes
from a correct-looking page that the file's own beaming was honoured.

**Alternative considered:** carry the file's beams through the importer as an
opaque per-note hint. Rejected: it is invariant 10 with a different name, and it
would make the renderer's output depend on which exporter wrote the file.

### 2. One group is one beat, or one dotted beat in a compound metre

`beatType >= 8 && beats % 3 == 0` is compound. That rule gets 3/8, 6/8, 9/8,
12/8 and 6/16 right, and deliberately excludes 6/4 — six quarter beats, not two
dotted ones.

Real engraving has more rules than this. 4/4 often beams four eighths across
half a bar; a syncopated figure is beamed to show the syncopation. Those are
judgements about the music, and the floor this sets — **the group never crosses
a beat** — is right everywhere and wrong nowhere. Widening it later is a change
to one function with its own tests.

**Alternative considered:** group by the bar in 3/8 and 6/8 as some editions do.
Rejected for 6/8; adopted for 3/8, where one group of three *is* the bar.

### 3. The geometry moves the beam, never bends it

A beam is a straight bar. When a note inside a group pokes through the line its
outer notes imply, the whole line is pushed away from the staff until every stem
clears, rather than one stem being shortened to meet a bent bar.

That is one `min` over the group and one shift, and it is the whole of why the
result is straight by construction instead of by hoping no case breaks it.

The rise is clamped before the shift, so a two-octave leap inside a beat gives a
beam with a slope rather than a diagonal.

### 4. `StemGlyph::tipPosition` becomes `tipSpaces`, a double

A stem's attach end is on a notehead, so it is a staff position. Its far end is
wherever the default length or a beam put it, and a sloped beam puts it between
positions. Keeping an integer position and adding a fractional override beside
it would be two sources of truth for one number.

The attach end stays a `StaffSteps`, because it genuinely is one.

### 5. The slot floor is derived, not configured twice

A flag hangs off the right of an up stem. `minimumSlotWidth` happened to be
wide enough for a notehead and a flag; nothing said it had to be. The floor is
now `max(minimumSlotWidth, noteheadWidth + flagWidth)`, so lowering one number
cannot quietly let a flagged eighth touch the note after it.

### 6. Rests and grace notes break a group, and that is reported

An engraver beams a beat holding a short rest, and beams a run of grace notes.
Both are real gaps. Both are reported rather than silently approximated, which
is the rule #200 states in the general case and the reason a partial pass is
shippable at all.

## Open questions

*Answered after implementation.*

**Does the beat-floor grouping look wrong anywhere in the corpus?** Not in what
`ScoreLayoutCorpusTests` lays out — every beam in every corpus file satisfies
the clamp, the minimum stem length and the level-coverage invariant. Whether any
of them looks *unidiomatic* to a reader is a question for a picture, and only
the two approved states are pictures. The fixture's two groups were examined and
are right.

**Should a beam group be allowed to cross a barline?** No, and it cannot:
grouping runs per measure. Cross-barline beaming exists in real engraving and is
rare enough that a group is better defined as a thing inside a bar.

**Is the partial-beam direction rule enough?** It is the conventional one —
towards the beat, approximated by "towards the note before it". A case it gets
wrong is a lone short note at the start of a group that belongs to the beat
*after* it, which cannot arise here because a group never crosses a beat.
