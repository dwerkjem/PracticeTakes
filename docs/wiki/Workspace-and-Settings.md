# Workspace and Settings

## Docking, floating and tiling

Each tool runs either inside the main window or in its own resizable floating
window. Moving it between the two doesn't restart it, so it keeps its analysis
and its history.

- **Tools → *tool* → Dock in workspace** or **Float in window** moves a running tool.
- Drag a tool's **Drag** handle into the main window. The targets that appear
  tile it **left**, **right**, **top** or **bottom**, group it as a **tab**, or
  send it back to a floating window.
- A tab drags the same way. Drop it on another tab strip to join that group.
  Drop it on its own strip, or on **Tile beside** over its own pane, and it
  comes out of the group and tiles beside the tab to its left (the tab to its
  right, if it was first). Drop it on an edge of its own pane to put it on that
  side instead.
- Split dividers are resizable.
- **Tools → Arrange workspace** does the same things from the keyboard.
- A tuner, spectrogram or harmonic analyzer can be open more than once, and each
  copy docks, floats and tiles on its own. See [[Tools]].

| Tiled | Tabbed |
|---|---|
| ![Three tools tiled side by side](images/overview.png) | ![Three tools sharing a tab strip: Tuner, Spectrogram, Harmonic Analyzer](images/tools-tabbed.png) |

## Workspaces

**Tools → Workspaces** has four built-in arrangements:

| Workspace | For |
|---|---|
| Vocal Warm-up | Singing exercises |
| Pitch Practice | Holding notes in tune (also what **Reset to Pitch Practice...** returns to) |
| Spectrum Analysis | Looking at tone |
| Performance Preparation | Running a piece |

You can also save the current arrangement under a name, apply it later,
overwrite or rename it, and delete it. Named workspaces are independent copies.
Resetting the workspace replaces only the *current* arrangement with Pitch
Practice; it doesn't touch your named workspaces or your preferences.

Your layout is restored the next time you start Practice Takes.

## Settings

**Settings** has five tabs:

| Tab | Holds |
|---|---|
| **Appearance** | **Language**, marked with a globe so it can be found in a language you do not read: the language Practice Takes speaks, each listed by its own name. **Automatic** follows your system when Practice Takes speaks it, and English otherwise; a change takes effect when Practice Takes restarts. It is also in the **Settings** menu, under the globe. Then the theme, **Dark** or **Light**, for every open window; and the **Text size**, from 80% to 150% in steps of 10 (100% by default), for menus, buttons, labels and everything the tools write. Under it, every size of text the application uses is shown at the size you chose, with its usual size beside it. The tuner's note name and the notation in the Score View keep their own sizes, since they already fill their panes. At a large size the menus fold into **Menu** sooner, a Settings page that no longer fits scrolls, and a small tool switches to its compact form sooner **Note names** chooses how every tool names a note: **C D E F G A B**, **German** (H is B natural and B is B flat, with Fis, Es and so on), or **fixed do** (Do Re Mi). **Octave numbers** chooses where the numbers start: middle C is **C4**, **C3**, or **c′** (Helmholtz). Both are your own, separate from the language the application is in. A note heard without a score to spell it is spelled as the open score's key would spell it, so a piece in F minor shows A flat rather than G sharp |
| **Audio** | Mute, input volume, the live input level, buffer health, the metronome's volume and accent (and why it's silent, if it is), and the input and output devices |
| **MIDI** | The keyboard and the sustain pedal (see [[MIDI Keyboard]]) |
| **Practice** | Practice presets, which set the tuner's behaviour for the material you're practising; how many bars the metronome counts in; the **reference pitch**, A4 = 440 Hz by default, from 415 to 466 Hz; **Near perfect within**, the green zone, ±5 cents by default (5 cents flat to 5 cents sharp), from ±1 to ±20; **In tune within**, how close a sung note must be to count as in tune, ±25 cents by default, from ±2 to ±49 and always wider than near perfect; **Out of tune beyond**, where amber turns red, ±40 cents by default, up to ±50 and always wider than in tune; **Ignore sound below**, the quietest sound a microphone take counts as playing or singing, −45 dB by default, from −80 to −20, with your microphone's level beside it and a line at the threshold, so you can set it against your room; whether a target you chose is kept when a score opens; whether a score's target **follows the note being sung** while the score plays (on by default); and whether **Record pressed during playback counts in first**, stepping back a bar, rather than recording on from where you are (off by default) |
| **Reset & support** | **Send feedback**, and reset the current tool, the audio settings, the layout, or all settings, each after a confirmation |

**Import settings**, **Export settings** and **Save settings** sit along the
bottom of every tab.

![The same three tools in the light theme](images/overview-light.png)

Every setting, settings tab and menu has an icon beside its words, so a
control can be found by its picture. In the title bar the transport's
buttons are icons alone; hold the pointer over one for its name.

## Moving settings to another machine

**Settings → Export settings** writes a human-readable `.ptsettings` file. It
contains every setting: appearance, the language and how notes are named,
audio (the input and output devices), the metronome, count-in and playback,
practice (reference pitch, the intonation limits, "Ignore sound below" and the
target note's options), the MIDI keyboard and its pedal threshold, fullscreen,
feedback invitations, tools, the active workspace and the named workspaces.

It leaves out what is not a setting:
- the note you chose to practise;
- feedback text and contact details;
- installation identity, use counters and invitation history;
- logs, benchmarks and analysis data.

**Import** checks and migrates the file, then shows what it would replace.
Nothing changes until you confirm. A confirmed import replaces every setting
and all named workspaces, and takes effect at once — except the language,
which changes the next time Practice Takes starts; the confirmation says so
when the file's language differs from yours. A file exported by an older
version, before every setting travelled, keeps this machine's values for the
ones it does not hold.

A file from a newer, unsupported version is refused with a compatibility error,
and nothing is changed. A missing audio device or an off-screen window position
falls back to a safe choice without rejecting the rest of the import, and a MIDI
keyboard that is not plugged in is remembered and opens when it is.
