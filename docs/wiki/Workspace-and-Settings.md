# Workspace and Settings

## Docking, floating and tiling

Each tool runs either inside the main window or in its own resizable floating
window. Moving it between the two doesn't restart it, so it keeps its analysis
and its history.

- **Tools → *tool* → Dock in workspace** or **Float in window** moves a running tool.
- Drag a tool's **Drag** handle into the main window. The targets that appear
  tile it **left**, **right**, **top** or **bottom**, group it as a **tab**, or
  send it back to a floating window.
- Split dividers are resizable.
- **Tools → Arrange workspace** does the same things from the keyboard.

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

**Settings** has five tabs: **Appearance**, **Audio**, **MIDI**, **Practice**,
and **Reset & support**.

## Moving settings to another machine

**Settings → Export settings** writes a human-readable `.ptsettings` file. It
contains your appearance, audio, fullscreen, feedback-invitation, tool,
active-workspace and named-workspace settings.

It deliberately leaves out:
- feedback text and contact details;
- installation identity, use counters and invitation history;
- logs, benchmarks and analysis data;
- your MIDI keyboard and pedal setting, because those belong to the machine the
  keyboard is plugged into.

**Import** checks and migrates the file, then shows what it would replace.
Nothing changes until you confirm. A confirmed import replaces your supported
preferences and all named workspaces, and takes effect at once.

A file from a newer, unsupported version is refused with a compatibility error,
and nothing is changed. A missing audio device or an off-screen window position
falls back to a safe choice without rejecting the rest of the import.
