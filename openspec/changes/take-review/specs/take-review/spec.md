## ADDED Requirements

### Requirement: A finished take is reviewed against the part it matches best
When a take ends and is kept, it SHALL become the take under review, judged
against the part of its score that it matches best. The reader SHALL be able to
review another kept take, or the same take against another part.

#### Scenario: A pianist's take
- **WHEN** a keyboard take of a voice-and-piano score ends
- **THEN** it is reviewed against the piano part

### Requirement: Every judged note is marked on the score
While a take is under review and its score is shown, the Score View SHALL mark
every note of the reviewed part as correct, wrong (with the played pitch),
missed, or with its timing only. It SHALL mark early and late notes, and every
extra note at its time. The marks SHALL be able to be turned off.

#### Scenario: A wrong note on the page
- **WHEN** a G was written and an A was played
- **THEN** the G is marked wrong and labelled A

### Requirement: A list of results moves the score to each one
The Take Review tool SHALL list every result in time order, extras included,
with its bar, beat, pitch, outcome and timing. Choosing a result SHALL move the
practice clock, and with it the Score View and transport, to that result's
place.

#### Scenario: Going to a mistake
- **WHEN** the reader clicks the row for a wrong note in bar 14
- **THEN** the clock moves to that note and the Score View shows it
