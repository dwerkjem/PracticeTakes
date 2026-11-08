# Getting Started

## Install

Published versions come from
[GitHub Releases](https://github.com/dwerkjem/PracticeTakes/releases) as native
installers:

| Platform | Package | What it does |
|---|---|---|
| Windows x64 and ARM64 | `.exe` installer | Bundles the runtime libraries and adds Practice Takes to the Start Menu |
| Debian 12+ and compatible Ubuntu, x64 and ARM64 | `.deb` | Installs its dependencies through APT and adds Practice Takes to the Applications menu |
| macOS Intel and Apple Silicon | `.pkg` | Installs into `/Applications` |

To run something newer than the latest release, build it from source. See
[[Contributing]] for the short version and
[`BUILDING.md`](https://github.com/dwerkjem/PracticeTakes/blob/main/docs/development/build/BUILDING.md)
for the full one.

## First run

With no score and no tool open, the window offers **Open the sample score**. If
there is no usable microphone, a warning in the corner says so and links to
Settings:

![The empty window: "Open a score to get started" with an Open the sample score button, and a No microphone detected warning](images/first-run.png)

1. **Choose your input.** Open **Settings → Audio** and pick a microphone. If
   there is no usable microphone, the main window shows a warning with a
   shortcut to Settings. **Input volume** is a software gain from 0 to 200%,
   shared by every tool, and never changes the operating system's own setting.

   ![Settings, Audio tab: Mute microphone, Input volume, Input level, buffer health and the Input device list](images/settings-audio.png)

2. **Open a tool.** The **Tools** menu opens the Tuner, Spectrogram, Harmonic
   Analyzer, Score View and Target Note. Every tool listens to the same
   microphone, so several can be open at once. See [[Tools]].
3. **Pick a workspace.** **Tools → Workspaces** has four built-in arrangements:
   *Vocal Warm-up*, *Pitch Practice*, *Spectrum Analysis* and *Performance
   Preparation*. See [[Workspace and Settings]].
4. **Open a score** (optional). **File → Open score...** reads `.musicxml`,
   `.xml` and `.mxl`. **File → Open sample score** opens a bundled Mahler song if
   you just want to see it work. See [[Scores and MusicXML]].
5. **Connect a MIDI keyboard** (optional). Choose it under **Settings → MIDI**.
   See [[MIDI Keyboard]].

## The menus and the title bar

| Menu | Holds |
|---|---|
| **File** | Open a score, or the bundled sample |
| **Settings** | Appearance, audio and the metronome, MIDI, practice presets and the count-in, reset and support, and portable settings import/export |
| **Tools** | Opens each tool, arranges the workspace, and manages named workspaces |
| **Help** | Send feedback, and turn feedback invitations on or off |

On the right of the title bar, the **microphone button** shows the input's state
and mutes or unmutes it in one click: *Mic active - Mute*, *Mic muted - Unmute*,
or *Mic disconnected*. **Fullscreen** fills the screen, and becomes **Restore**.

While the microphone is muted, the button turns amber and an amber frame
labelled **Microphone muted** surrounds the whole workspace, so you can't sing
into a muted microphone without noticing:

![The workspace framed in amber, labelled Microphone muted, with the title bar's button reading Mic muted - Unmute](images/microphone-muted.png)

## Playing along with a score

Once a score is open, the title bar shows the **transport** between the menus
and the microphone button. It drives one practice clock, which every tool
shares:

| Control | Does |
|---|---|
| **Play** / **Pause** | Starts the clock from where it is, counting in first, and holds it where it is. Pausing keeps your place. |
| **Stop** | Stops, and goes back to the start of the score, or of the loop |
| **Bar 3, beat 2** | Where the clock is in the score, or which count-in beat is sounding |
| **100%** | The tempo, as a percentage of the score's own, from 5% to 200% in steps of 5. **−** and **+** change it, even while playing, and the music carries on from the same beat. |
| **Click** | The metronome, on every beat, with the first beat of each bar accented |
| **Loop** | Plays the score round and round, starting again on exactly the same beat however many times it loops |

The clock follows the score's own tempo changes and time signatures. A bar of
7/8 clicks seven times. The **count-in** is one bar by default, at the tempo
and time signature of the point you start from. You can change it to none or up
to four bars in **Settings → Practice**.

The metronome plays through the output device chosen in **Settings → Audio**,
where you also set its volume and turn the accent on or off. Muting the
microphone doesn't mute the metronome. With no output device, the clock still
runs, and Settings says why the metronome is silent.

![The title bar's transport while the sample score plays: Pause, Stop, Bar 3 beat 2, 100%, Click and Loop](images/transport-playing.png)

## Telling us what you think

After a few completed tool uses, Practice Takes may ask once whether you'd like
to share feedback; **Help → Disable feedback invitations** turns that off. A
report is submitted online when it can be, and kept as a local draft when it
can't.
