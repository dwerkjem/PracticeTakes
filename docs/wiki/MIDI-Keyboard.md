# MIDI Keyboard

A digital piano or MIDI keyboard is one of the MVP's two inputs; the other is a
singer's microphone. With a keyboard, Practice Takes knows exactly which keys
you pressed, when, how hard, and what the sustain pedal was doing. It doesn't
have to guess any of that from a microphone.

## Connecting

1. Connect the keyboard, over USB or a MIDI interface.
2. Open **Settings → MIDI** and choose it under **Keyboard**. If it isn't
   listed, connect it and press **Rescan**.
3. Play a key. The panel confirms what it received. Until then it says "Nothing
   received yet. Play a key to check the connection."

The choice is remembered. If the keyboard isn't plugged in when Practice Takes
starts, the panel says it will be used as soon as it appears. If you unplug it
and plug it back in, it reconnects by itself, and Practice Takes never quietly
switches to a different device in its place.

The keyboard choice is not included in exported settings. It belongs to the
machine the keyboard is plugged into.

## The sustain pedal

**Pedal counts as down at** sets the point at which a half-pressed pedal counts
as down. Pedals differ, and some send many in-between values.

On a piano, a key released while the pedal is down keeps sounding until the
pedal comes up. Practice Takes follows that. A note held by the pedal is shown as
"held by pedal", not as short. Two cases are shown as *not measured* rather than
guessed:

- **The pedal hasn't been seen yet.** A pedal only reports when it moves, so
  until you move it once, its position is unknown.
- **Events were lost, or the keyboard disconnected** partway through a note.

Pedal markings in a score are not read, so pedal use is shown but never
compared with the score.

## What it is used for, now and next

Today the capture is live and visible in **Settings → MIDI**. The MVP uses it to
judge each take against the score: wrong, missed, extra, early and late notes,
looped one hand at a time. See the [[Roadmap]].

An acoustic piano through a microphone is a deliberate deferral. Telling which
notes of a chord sounded, through the pedal, from audio alone is a research
problem
([#204](https://github.com/dwerkjem/PracticeTakes/issues/204)).
