# score-part-selection Specification

## Purpose
Choosing which parts of the open score the Score View draws: a Parts menu, the
layout drawing only the parts shown as if the score held only those, at least
one part always shown, and the choice remembered per Score View by part id and
name.
## Requirements
### Requirement: Parts can be shown and hidden in the Score View
The Score View SHALL offer, for a score with more than one drawable part, a
"Parts" choice in its options menu with one entry per part, named as the part
is labelled beside its staves and ticked when the part is shown. Every part
SHALL be shown by default.

#### Scenario: A score for voice and piano
- **WHEN** a score with a voice part and a piano part is open
- **THEN** the options menu lists "Voice" and "Piano" under "Parts", both ticked

#### Scenario: A single part
- **WHEN** the open score has one drawable part
- **THEN** the options menu offers no "Parts" choice

### Requirement: Hidden parts are not drawn
The Score View SHALL draw a hidden part not at all. It SHALL lay out the parts
shown as if the score held only those, and everything that depends on which
parts are drawn SHALL follow the parts shown: part names, braces, barlines
between parts, the tempo mark drawn above the top part, pagination, and the
title strip's list of parts.

#### Scenario: Hiding the voice
- **WHEN** the voice is hidden in a score for voice and piano
- **THEN** only the piano's staves are drawn, they fill each line as a piano
  score would, the tempo mark is drawn above them, and the title strip names
  only the piano

#### Scenario: A hidden part says nothing
- **WHEN** a hidden part has content the view would otherwise report as not
  drawn
- **THEN** no notice about that part is shown

### Requirement: At least one part stays shown
The Score View SHALL NOT allow every part to be hidden: the entry of the last
part shown SHALL be disabled. A saved selection that would hide every part of
the open score SHALL be ignored, and every part shown.

#### Scenario: The last part shown
- **WHEN** only the piano is shown
- **THEN** its entry under "Parts" is disabled

#### Scenario: A selection that hides everything
- **WHEN** a saved selection would hide every part of the open score
- **THEN** every part is shown

### Requirement: A selection applies only to the parts it names
A saved selection SHALL hide a part only when the open score has a part with
both the id and the name that were hidden.

#### Scenario: The same file reopened
- **WHEN** a part is hidden and the same file is imported again
- **THEN** that part is still hidden

#### Scenario: Another score with the same ids
- **WHEN** a part with id P2 named "Alto" is hidden, and a score is opened whose
  part P2 is named "Piano"
- **THEN** the piano is shown
