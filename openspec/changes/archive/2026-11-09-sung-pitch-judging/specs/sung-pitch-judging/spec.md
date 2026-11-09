## ADDED Requirements

### Requirement: A sung note is judged sharp, flat or in tune
For each single written note that a microphone take's timing matched, the
judgement SHALL measure the signed deviation in cents of the note's sure
sustain frames from the written pitch, at the reference pitch the take was sung
against. It SHALL call the note in tune within the tolerance, sharp or flat
beyond it, and wrong when it is nearer another note.

#### Scenario: Consistently sharp
- **WHEN** a singer holds every note of a phrase 35 cents sharp, with a
  tolerance of 25 cents
- **THEN** every note is judged sharp, by about 35 cents

#### Scenario: Steady and in tune
- **WHEN** a note is held within 5 cents of its written pitch
- **THEN** it is judged in tune, with a score near 1

### Requirement: Transitions do not decide a note
A note's verdict SHALL rest on its sustain, not on the attack or the release,
so that a scoop into a note or a slide out of it does not change the verdict.

#### Scenario: A scoop
- **WHEN** a singer scoops up a semitone into a note and then holds it in tune
- **THEN** the note is judged in tune

### Requirement: Low confidence is uncertain, not wrong
A note with too few sure frames SHALL be reported as uncertain, and SHALL NOT
count as wrong.

#### Scenario: Breathy or quiet
- **WHEN** a note's audio is too noisy for the pitch tracker to be sure of
- **THEN** the note's pitch is uncertain, and it is not counted wrong

### Requirement: An octave error is not a wrong note
A deviation SHALL be folded by whole octaves before it is judged, and the
octave the note was sung in SHALL be reported.

#### Scenario: A tenor on a treble line
- **WHEN** a singer sings a treble part an octave below, in tune
- **THEN** each note is in tune, and noted as an octave low

### Requirement: The tolerance is the singer's
The in-tune tolerance SHALL be set in Settings, from 5 to 50 cents, and
changing it SHALL re-judge the take under review.

#### Scenario: A stricter tolerance
- **WHEN** the tolerance changes from 25 to 10 cents while a take with notes
  15 cents sharp is under review
- **THEN** those notes change from in tune to sharp

### Requirement: The review shows intonation
The Take Review list SHALL show each sung note's verdict and deviation. The
Score View SHALL mark a sharp or flat note distinctly from an in-tune one.

#### Scenario: Reading a sung take
- **WHEN** a sung take with sharp and flat notes is under review
- **THEN** the list reads, for example, "18 ¢ sharp", and the Score View shows
  an upward arrow on that note
