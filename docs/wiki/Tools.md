# Tools

Every tool opens from the **Tools** menu, can be docked in the main window or
floated in its own, and keeps running while you move it. Every microphone tool
shares the one input chosen in **Settings → Audio**. Each tool's options menu
also has **Give feedback on this tool**.

## Tuner

Listens to the microphone and shows the detected note, its frequency, and how
many cents sharp or flat it is.

- **Display mode:** a bar or a meter.
- **Graph:** pitch over time, so drift within a held note is visible.
- **Advanced settings** tune how the reading behaves: *pitch easing* (smoothing),
  *average window*, *note switch* (how readily it moves to a new note), *dropout
  hold* (how long it holds through a gap) and *graph duration*.
- **Settings → Practice → Practice preset** applies a coordinated set of these
  controls for the material you are practising.
- When the tool is small it shows less detail rather than cramming everything in.

It needs a sustained note: "Play or sing a sustained note" is what it says until
it hears one.

## Spectrogram

A scrolling frequency view of the microphone. It is useful for looking at
harmonics, noise, resonance and how a tone changes over time.

## Harmonic Analyzer

Shows the relative strength of a sustained note's harmonics over time, with its
fundamental and a brightness reading. It tells you when it can't give a clean
answer, rather than guessing:

- **"Weak fundamental"**: the profile is anchored to the strongest overtone.
- **"Uncertain or inharmonic input"**: the sound isn't a clean pitched tone.
- **Clipping**, **muted** and **disconnected** states are shown as they happen.

## Score View

Draws the open score as engraved notation: staves and grand staves, clefs, key
and time signatures, notes, rests, stems, beams, ties, lyrics, dynamics and tempo
marks, with each part named beside its staves.

- **Three views:** *Page* (the default, with previous and next page), *Vertical
  scroll* and *Horizontal scroll*.
- **Enlarge**, **Shrink** and **Default size** change the size of the music.
- **Parts** shows and hides individual parts.

Open a score with **File → Open score...**. Until then it says "No score open".

## Target Note

Shows one note of the open score as text: its bar, where it starts, how it is
written and the MIDI note it sounds as. It was built to prove the score model
could be read by a tool. It is due to be replaced by a target note the whole
application shares, which the tuner will show beside its own reading
([#227](https://github.com/dwerkjem/PracticeTakes/issues/227)).

## Coming next

Every planned feature is on the [[Features]] page. Among the first:

- **Choose a target note** on a keyboard, a staff or the score, or by singing it,
  and **see the note you're on** against it, on a piano keyboard and a staff
  ([#296](https://github.com/dwerkjem/PracticeTakes/issues/296),
  [#297](https://github.com/dwerkjem/PracticeTakes/issues/297)).
- **Your vocal range**, found, kept, and drawn against a piece
  ([#298](https://github.com/dwerkjem/PracticeTakes/issues/298)).
- **A reference tone or drone** to sing in tune against
  ([#299](https://github.com/dwerkjem/PracticeTakes/issues/299)).
- **The same tools on a recording.** Record a take, or open an audio file, and
  look at it with every tool above, exactly as you would live: play it, pause
  it, scrub it. Graphs show a whole take at once, with the score's target notes
  laid over the pitch curve
  ([#291](https://github.com/dwerkjem/PracticeTakes/issues/291)).
- **More analysis tools**, each built together with the evaluation that needs
  it: a loudness meter, a sustain and legato analyzer, and a tempo and
  rhythm-stability tool.

The [[Roadmap]] has the order.
