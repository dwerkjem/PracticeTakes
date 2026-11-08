# Tools

Every tool opens from the **Tools** menu, can be docked in the main window or
floated in its own, and keeps running while you move it. Every microphone tool
shares the one input chosen in **Settings → Audio**. Each tool's options menu
also has **Give feedback on this tool**.

## Tuner

Listens to the microphone and shows the detected note, its frequency, and how
many cents sharp or flat it is.

![The tuner in its graph view, reading A4 with the pitch line drifting just above and below it](images/tuner-graph.png)

- **Display mode** picks one of three views:
  - **Graph**, pitch over time, so drift within a held note is visible (above);
  - **Bar**, the cents offset as a dot on a line from −50 to +50;
  - **Meter**, a needle between *flat* and *sharp*.

| Bar | Meter |
|---|---|
| ![The tuner's bar view, a dot a few cents left of zero](images/tuner-bar.png) | ![The tuner's meter view, a needle just left of centre](images/tuner-meter.png) |

- **Advanced settings**, from the tool's **...** menu, tune how the reading
  behaves: *pitch easing* (smoothing), *average window*, *note switch* (how
  readily it moves to a new note), *dropout hold* (how long it holds through a
  gap) and *graph duration*. **Clear graph** starts the graph afresh.

  ![The tuner with its advanced settings open: five sliders under the graph, and a Clear graph button](images/tuner-advanced.png)

- **Settings → Practice → Practice preset** applies a coordinated set of these
  controls for the material you are practising.
- When the tool is small it shows less detail rather than cramming everything in.

It needs a sustained note: "Play or sing a sustained note" is what it says until
it hears one.

## Spectrogram

A scrolling frequency view of the microphone. It is useful for looking at
harmonics, noise, resonance and how a tone changes over time. Frequency is
marked from 100 Hz to 10 kHz on a logarithmic scale, rising up the screen. In a
tall, narrow panel the view turns on its side, so frequency runs left to right
and its labels read downwards, as in the picture on [[Home]].

![The spectrogram with a sung note: a bright band near 440 Hz and its overtones stacked above it](images/spectrogram.png)

## Harmonic Analyzer

Shows the relative strength of a sustained note's harmonics over time, with its
fundamental and a brightness reading. The bars are the first eight harmonics now;
the graph below is the same harmonics over time.

![The harmonic analyzer: a fundamental of 215 Hz, eight harmonic bars falling away from H1, and their history as lines](images/harmonic-analyzer.png)

It tells you when it can't give a clean answer, rather than guessing:

- **"Weak fundamental"**: the profile is anchored to the strongest overtone.
- **"Uncertain or inharmonic input"**: the sound isn't a clean pitched tone.
- **Clipping**, **muted** and **disconnected** states are shown as they happen.

## Score View

Draws the open score as engraved notation: staves and grand staves, clefs, key
and time signatures, notes, rests, stems, beams, ties, lyrics, dynamics and tempo
marks, with each part named beside its staves.

![The score view showing the sample song for voice and piano, page 1 of 4](images/score-view.png)

- **Three views:** *Page* (the default, with previous and next page), *Vertical
  scroll* and *Horizontal scroll*.
- **Enlarge**, **Shrink** and **Default size** change the size of the music.
- **Parts** shows and hides individual parts.
- **Fit width** and **Fit page** (Page view) size the page to the panel's width,
  or show the whole page at once.

### Following the practice clock

Play a score with the transport in the title bar (see [[Getting Started]]) and
the Score View follows it:

- A **cursor** marks where the clock is, moving through each note as it sounds.
- The **notes sounding** are tinted, and the **notes coming next** are ringed.
  **Look ahead** in the options menu sets how many onsets ahead, from none to
  four (two by default). Only the parts you show are marked, so hide the piano
  to see only your line.
- The view **follows the clock**. It turns the page in Page view, steps a
  system at a time in Vertical scroll, and moves a screen at a time in
  Horizontal scroll. Scroll or turn a page yourself to look elsewhere: it stops
  following, and a **Follow** button appears in the header until you press it
  or play again. **Follow the practice clock** in the options menu turns it off
  altogether.
- **Click a note** to move the clock there, and **Go to bar…** to move it to a
  bar by its printed number. Every Score View, and the transport, follow.

![The score view with the clock at bar 6: a band over the first beat, its notes tinted and the next two onsets ringed](images/score-cursor.png)

Open a score with **File → Open score...**. Until then it says "No score open".
The header names the work and its parts, and links to the import's notes when
there are any (*8 import diagnostics* above). A line at the bottom says where the
drawing knowingly differs from the file, such as beams regrouped by the time
signature.

## Target Note

Shows one note of the open score as text: its bar, where it starts, how it is
written and the MIDI note it sounds as. It was built to prove the score model
could be read by a tool. It is due to be replaced by a target note the whole
application shares, which the tuner will show beside its own reading
([#227](https://github.com/dwerkjem/PracticeTakes/issues/227)).

## Two of the same tool

The tuner, the spectrogram and the harmonic analyzer can each be open more than
once: two tuners side by side, or two spectrograms at different settings. Each
copy has its own settings, its own analysis and its own place in the workspace.

- **Tools → *tool* → Open another** opens a new copy. **Open or focus** still
  goes to the one already open.
- Copies after the first are numbered: *Tuner*, *Tuner 2*.
- **Rename...**, in the Tools menu or the tool's own **...** menu, gives a copy
  a name of your own, such as *Violin*. Leave the name empty to go back to the
  number. Names are saved with the workspace.
- Once two copies are open, the tool's Tools submenu lists each one by name,
  with its own Dock, Float, Rename and Close.

![Two tuners tiled side by side, named Tuner and Tuner 2](images/two-tuners.png)

![The Tools menu with two tuners open: Open or focus, Open another, then an entry for Tuner and one for Tuner 2](images/tools-menu-instances.png)

You can have up to six copies of each tool. At six, **Open another** is greyed
out and says *6 is the most*.

Spectrograms and harmonic analyzers each take one of 16 audio streams from the
microphone. Tuners don't take one, because all tuners share one pitch reading.
If the streams ever run out, **Open another** says *no audio stream free*.
Choosing it tells you which tools are using them, so you know what to close.

The score view and the target note tool open once each, because every copy
would show the same score.

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
