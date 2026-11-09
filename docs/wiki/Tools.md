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

### The target note

Above the reading, the tuner names the note you're aiming at and where you are
against it:
- *Target E4 · on it*, within 5 cents, in green;
- *Target E4 · 16 ¢ flat*, within half a semitone;
- *Target E4 · 2 semitones below*, further away.

When a score opens, the target is its first note: the first note of its first
part that plays one note at a time, such as the voice of a song. A piano piece
has none, and the tuner says so, because a target is one note.

**Choose the target yourself** by clicking the target line, or with **Choose
target…** in the tuner's **...** menu:
- **click a key** on the keyboard;
- **click the staff**, after choosing ♭, ♮ or ♯ beside it;
- **type a name**, such as *F#4* or *Bb3*, and press **Set**;
- **sing or play it**, then press **Use the note I'm on**. It works once you
  have held one note steadily, within 30 cents, for a moment;
- **step** a semitone up or down, or a step of the score's key.

In the Score View, **right-click a note** and choose **Aim at G4**. For a chord,
pick which of its notes.

A target you choose is remembered when you quit. Opening a score replaces it
with the score's first note, unless **Settings → Practice → Keep my target when
a score opens** is on.

![The target picker under the tuner's target line: a keyboard, a staff, a name field, Use the note I'm on, and step buttons](images/target-picker.png)

The tuner measures against the **reference pitch**, A4 = 440 Hz unless you
change it in **Settings → Practice** (from 415 to 466 Hz).

![The tuner with a target: Target E4, 16 cents flat, from the sample song](images/tuner-target.png)

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

### Marking a take

While a take is under review and its score is open, the Score View marks every
note of the part it was judged against: right, wrong (with the key played),
missed, early or late, and a cross above the staff for each extra key. See
[[Getting Started]] and the Take Review below. **Show take review** in the
options menu turns the marks off.

## Take Review

The take you just played, judged note by note. It shows:
- the take and the part it was judged against, each of which you can change;
- a summary: *158 correct · 7 wrong · 11 missed · 1 extra · 3 early · 4 late*;
- every result in time order.

Click a result to move the practice clock, and with it the Score View, to that
note. Before the first take it says how to record one.

![The Take Review beside the Score View: a piano take stopped in bar 8, its notes marked and listed](images/take-review.png)

## Target Note (retired)

The Target Note tool showed the first note of the open score. It was replaced by
the target line in the tuner (above), which every tool can read. A workspace
saved with the Target Note tool opens a tuner in its place.

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
