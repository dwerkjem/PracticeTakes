## Decisions

### 1. A tool of its own

**Note View** (`note-view`) reads pitch the way the tuner does: from
`SharedPitchAnalysis`, through a `PitchTracker` of its own, at the application's
reference pitch. So the two agree to the cent. It is multi-instance like the
tuner, reads no audio stream, and keeps its clef choice as its settings.

### 2. The JUCE-free model

`src/features/notes/NoteViewModel.h`:
- **The keyboard window:** two octaves from a C, chosen to hold the current
  note, the target and every held key. It moves only when one of them leaves
  it, so the keys don't jump while you sing.
- **Key geometry:** a white key's left edge, and a black key's, in units of
  white keys. A fractional MIDI pitch maps to a continuous x between key
  centres, so the trail is a line, not steps.
- **The trail:** the last three seconds of (time, pitch) points, with a gap
  wherever there was no pitch.
- **Shading:** a lit key's colour runs from green within ±5 ¢, the tuner's
  in-tune band, to amber at ±50 ¢.

`pitch::staffStepsOf` (beside `pitchAtStaffPosition`) places a pitch in a clef:
treble, bass, or treble an octave down for tenor voices and guitar.

### 3. The clef

*Automatic*, the default, takes the target's part's first clef when the target
came from the score. Otherwise it is treble when the current note or target is
at or above middle C, and bass below it. The tool's options menu can fix
treble, bass or treble 8vb.

### 4. The MIDI keyboard

`ToolServices::midi` is the application's `MidiInputService`. The Note View
registers a consumer, keeps the set of keys down from note-ons and note-offs
(the pedal does not count: it shows the hand, not the sound), and lights them
in a second colour. A tool that never registers sees nothing.

### 5. Small and docked

Below 240 × 160 px, the staff and trail give way and only the keyboard and the
note's name show, like the tuner's compact layout.
