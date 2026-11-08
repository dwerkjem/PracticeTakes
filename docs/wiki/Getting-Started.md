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

1. **Choose your input.** Open **Settings → Audio** and pick a microphone. If
   there is no usable microphone, the main window shows a warning with a
   shortcut to Settings. **Input volume** is a software gain from 0 to 200%,
   shared by every tool, and never changes the operating system's own setting.
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

## The four menus

| Menu | Holds |
|---|---|
| **File** | Open a score, or the bundled sample |
| **Settings** | Appearance, audio, MIDI, practice presets, reset and support, and portable settings import/export |
| **Tools** | Opens each tool, arranges the workspace, and manages named workspaces |
| **Help** | Send feedback, and turn feedback invitations on or off |

## Telling us what you think

After a few completed tool uses, Practice Takes may ask once whether you'd like
to share feedback; **Help → Disable feedback invitations** turns that off. A
report is submitted online when it can be, and kept as a local draft when it
can't.
