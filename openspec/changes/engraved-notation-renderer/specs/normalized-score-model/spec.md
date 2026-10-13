## ADDED Requirements

### Requirement: The model converts a measure-relative onset to an absolute one
The model SHALL provide a function that returns an event's absolute position in
the score from the event and its containing measure, so that no consumer
computes `measure.start + event.onset` itself.

#### Scenario: An event's absolute onset is available directly
- **WHEN** a consumer needs the absolute tick of an event in measure 12
- **THEN** the model returns it from the measure and the event, without the
  consumer performing the addition

#### Scenario: The first measure does not hide the mistake
- **WHEN** absolute onsets are computed for events in the first measure and in a
  later measure
- **THEN** both are correct, and a consumer that used the measure-relative onset
  alone is distinguishable from one that did not in the later measure

### Requirement: The model provides event traversal in musical order
The model SHALL provide a way to visit the events of a part in musical order —
ascending absolute onset across measures, and across the voices within a
measure — yielding each event together with its measure, its voice, and its
absolute onset. Consumers SHALL NOT need to write their own nested walk over
parts, measures, voices, and events.

#### Scenario: Traversal crosses measures in order
- **WHEN** a part's events are traversed
- **THEN** every event of measure *n* is yielded before any event of measure
  *n+1*, and within a measure events are yielded in ascending absolute onset

#### Scenario: Traversal interleaves voices by onset
- **WHEN** a measure holds two voices whose events interleave in time
- **THEN** traversal yields them in ascending onset order regardless of voice,
  and ties between equal onsets are broken by a stated, repeatable rule

#### Scenario: Each visited event carries its context
- **WHEN** traversal yields an event
- **THEN** the event's measure, voice, and absolute onset are available without
  the consumer tracking them separately

### Requirement: The order of a chord's notes is a stated guarantee
The model SHALL state the order in which the notes of a chord event appear in
`ScoreEvent::notes`, and that order SHALL be the order of the source file rather
than any musical ordering. A consumer that requires a pitch ordering SHALL sort
for itself.

#### Scenario: Chord notes keep source order
- **WHEN** a chord is imported whose source writes its notes in an order other
  than ascending pitch
- **THEN** `notes` holds them in the source's order, and this is asserted by a
  test rather than left as an accident of the importer

#### Scenario: The first note of a chord is not the lowest by guarantee
- **WHEN** a consumer needs the lowest-sounding note of a chord
- **THEN** the documented contract directs it to sort, and does not permit
  `notes.front()` to be assumed to be lowest
