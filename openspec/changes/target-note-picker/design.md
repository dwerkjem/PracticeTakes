## Decisions

### 1. The picker is a call-out from the tuner

The tuner shows the target, so it is where the target is changed. Clicking the
target line, or **Choose target…** in the tuner's options menu, opens a call-out
(as Mix does) holding:
- a piano keyboard (JUCE's `MidiKeyboardComponent`, C2–C7, display only);
- a grand staff to click;
- a name field;
- **Use the note I'm on**;
- ▲ and ▼ for a semitone, and **Key ▲** and **Key ▼** for a step of the key.

Each writes the shared target at once. The picker keeps no copy, so every tool
reading the target updates on its next tick.

### 2. What is JUCE-free and tested

`src/platform/pitch/TargetChoice.h`:
- **`parseNoteName`**: a letter, any of `#`, `b`, `##`, `bb` (and ♯ and ♭), and
  an octave from −1 to 9. Spaces and case are forgiven.
- **`pitchAtStaffPosition`**: a treble or bass staff, a step count from the
  middle line, and an accidental give a `score::Pitch`.
- **`stepBySemitone`** spells the result sharp going up and flat going down.
- **`stepInKey`** moves to the next or previous scale degree of a major key
  given in fifths (the score's key at bar 1, or C), keeping the key's
  spelling.

### 3. "Use the note I'm on" waits for a steady reading

It is enabled only when all of these hold:
- the tuner has a note;
- the reading is within 30 ¢ of that note;
- it has held that note for at least 300 ms.

It takes the nearest note, spelled with sharps. An unsteady or doubtful reading
leaves the button disabled rather than picking a wrong note.

### 4. The target knows where it came from

`pitch::Target` gains its origin, **score** or **chosen**. When a score opens:
- **the default:** it replaces the target, as before #296;
- **with Settings → Practice → *Keep my target when a score opens* on:** a
  chosen target stays, and a target that came from the previous score is still
  replaced.

### 5. The chosen target is saved locally

The chosen target's spelling and the keep setting are saved in application
settings, like the reference pitch.

The issue asked for the target to persist "with the workspace". Workspaces
hold tool layouts, and a person switching between their layouts would not
expect their target to change; the acceptance criterion is that it "survives a
restart", which this does. Recorded here as a deliberate change.

### 6. From the score: right-click

A left click in the Score View moves the clock (score-cursor). A right-click on
a note opens a menu:
- **Aim at G4**, or for a chord each of its notes;
- **Move the clock here**, so the right-click menu also does what a click does.

A transposing part's note is offered as its written name, and the target keeps
both names.
