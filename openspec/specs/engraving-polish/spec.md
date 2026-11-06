# engraving-polish Specification

## Purpose
What takes the Score View from readable to a published page: accidentals and
seconds that never collide, ties drawn across a system break, ending brackets
and jump marks, ledger lines shared within a chord, text placed clear of the
notes, justified systems with an indented first system, multi-measure rests,
and system breaks that prefer a structural boundary.
## Requirements
### Requirement: Accidentals in a chord do not collide
The score view SHALL place every accidental so that it overlaps no other
accidental and no notehead, stacking a chord's accidentals into columns to the
left of the chord when one column cannot hold them.

An accidental that overlaps a neighbour reads as belonging to it, and a sharp
read on the wrong note is a wrong note played.

#### Scenario: Every note of a triad carries an accidental
- **WHEN** a chord of three notes a third apart each carries a sharp
- **THEN** the three sharps are drawn in at least two columns, none overlapping
  another, all to the left of the chord

#### Scenario: An accidental on the displaced note of a second
- **WHEN** the displaced note of a second carries an accidental
- **THEN** the accidental is drawn clear of both noteheads, to the left of the
  chord, rather than in front of the note it belongs to

#### Scenario: A single accidental reserves only its own width
- **WHEN** a note carries one accidental and nothing else in its column does
- **THEN** the room reserved before the note is that accidental's width and gap,
  not the width a stack of several would need

### Requirement: A second is displaced to the side away from its stem
In a chord containing two notes a second apart, the score view SHALL draw the
upper of the two to the right of the lower, displacing the upper note when the
stem points up and the lower note when it points down, so that both noteheads
touch the stem.

#### Scenario: A second under an up stem
- **WHEN** a chord with a second has its stem up
- **THEN** the lower note sits on the stem's left and the upper note on its
  right

#### Scenario: A second under a down stem
- **WHEN** a chord with a second has its stem down
- **THEN** the upper note sits on the stem's right and the lower note on its
  left, and neither notehead is drawn apart from the stem

### Requirement: A tie across a system break is drawn
A tie whose two notes fall on different systems SHALL be drawn as two halves:
from the first note to the end of its system, and from the start of the next
system's content to the second note.

A tie the reader cannot see reads as two struck notes.

#### Scenario: A tie crosses a break
- **WHEN** a tied pair's notes are laid out on consecutive systems
- **THEN** the first system ends with a half tie from the first note, and the
  next begins, after its clef and key signature, with a half tie into the second

#### Scenario: Nothing is reported for it
- **WHEN** every tie in a score is drawn, including those across breaks
- **THEN** no notice says ties were not drawn

### Requirement: Endings are drawn as volta brackets
The score view SHALL draw a bracket over each run of consecutive measures that
share the same ending numbers, labelled with those numbers, closed at its right
end when the run ends on a repeat barline and open otherwise.

Without them, a first and a second ending read as two bars played in turn,
which is different music from what the file says.

#### Scenario: A first and a second ending
- **WHEN** one measure belongs to ending 1, ends on a repeat barline, and is
  followed by a measure belonging to ending 2
- **THEN** two brackets are drawn: "1." closed at its right end, and "2." open

#### Scenario: A shared ending
- **WHEN** a measure belongs to endings 1 and 2 together
- **THEN** one bracket is drawn, labelled "1. 2."

#### Scenario: An ending across a system break
- **WHEN** a run of ending measures is split across two systems
- **THEN** the bracket continues onto the second system without repeating its
  label

### Requirement: Jump markings are drawn
The score view SHALL draw every jump marking the file gives: a segno or coda as
its sign at the start of its measure, and any other marking as text at the end
of its measure. A marking it does not recognise SHALL be drawn as text rather
than omitted.

A jump the reader cannot see sends them the wrong way through the piece.

#### Scenario: A segno and a "D.S. al Coda"
- **WHEN** one measure carries a segno and a later one "D.S. al Coda"
- **THEN** the segno sign is drawn at the start of the first, and the text at
  the end of the second

#### Scenario: An unrecognised marking
- **WHEN** a measure carries a jump marking the view has no sign for
- **THEN** its text is drawn, and nothing about it is silently dropped

### Requirement: Text does not collide with the notes
Measure numbers, tempo marks, volta brackets and jump marks above the staff,
and lyrics and dynamics below it, SHALL be placed clear of every notehead, ledger
line, stem, beam and accidental on the same system.

#### Scenario: A low note under a lyric line
- **WHEN** a system holds a note with ledger lines below the staff and has
  lyrics
- **THEN** the lyrics are drawn below that note's lowest ledger line

#### Scenario: Only the system that needs it moves
- **WHEN** one system has a note far below the staff and the next does not
- **THEN** only the first system's lyrics are moved down

#### Scenario: Lyrics and dynamics move together
- **WHEN** the height of a lyric row is changed
- **THEN** the dynamics below the lyrics move by the same amount and do not
  overlap them

### Requirement: Ledger lines are shared within a chord
Notes of one chord on the same side of the staff SHALL share their ledger lines:
one line at each needed position, wide enough for every notehead that uses it.

#### Scenario: Two notes above the staff
- **WHEN** a chord holds two notes that both need the first ledger line above
- **THEN** that line is drawn once

#### Scenario: A displaced note beyond the staff
- **WHEN** the displaced note of a second needs a ledger line
- **THEN** the line under it extends to cover it rather than a second line being
  drawn beside the first

### Requirement: Full systems are justified
Every system except the last SHALL be stretched to the available width by
widening the space between notes in proportion, leaving clefs, key signatures,
time signatures and repeat barlines at their natural width. The last system
SHALL be left at its natural width.

#### Scenario: A full system
- **WHEN** a system that is not the last is laid out
- **THEN** its final barline sits at the available width

#### Scenario: The last system
- **WHEN** the last system holds less than a full width of music
- **THEN** it ends where its music ends, not stretched across the width

#### Scenario: Justification is deterministic
- **WHEN** the same score is laid out twice at the same width
- **THEN** every position is identical

### Requirement: The first system is indented and names the part
The first system SHALL begin at an indent, with the part's name set in it. Every
later system SHALL begin at the margin.

#### Scenario: The opening of a score
- **WHEN** a score is drawn
- **THEN** its first system starts at the indent with the part name beside it,
  and its second system starts at the margin

### Requirement: Consecutive empty bars are condensed
Two or more consecutive measures that each hold only a whole-bar rest, and carry
no clef, key or time change, repeat, ending, jump or direction, SHALL be drawn
as one multi-measure rest showing their count.

Anything a reader needs to see ends the run, because condensing it would hide
it.

#### Scenario: Four empty bars
- **WHEN** four consecutive measures each hold only a whole-bar rest and nothing
  else
- **THEN** they are drawn as one bar showing a multi-measure rest of 4

#### Scenario: A key change inside the run
- **WHEN** the third of four empty bars changes key
- **THEN** the run is broken there, and the key change is drawn

#### Scenario: The bars can still be found
- **WHEN** a run of measures is condensed
- **THEN** the system holding it still reports the full range of measure
  indices it covers

### Requirement: A system break prefers a structural boundary
When a system must break, and one of the last two measures that fit ends on a
repeat barline or the end of an ending, the break SHALL move back to it,
provided the system it leaves is at least three quarters full.

#### Scenario: A repeat barline one bar back
- **WHEN** the fill would break one measure after a repeat barline, and breaking
  at the barline leaves the system more than three quarters full
- **THEN** the system breaks at the repeat barline

#### Scenario: It would leave the system too empty
- **WHEN** moving the break back to a repeat barline would leave the system less
  than three quarters full
- **THEN** the system breaks where the fill ran out
