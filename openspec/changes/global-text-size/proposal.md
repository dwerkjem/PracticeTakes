## Why

Every size of text in Practice Takes is fixed: 72 font sizes written into the
code across 19 files, plus the sizes the look-and-feel gives JUCE's own
buttons, labels and menus. Someone who reads the application from a music
stand, or on a small high-resolution laptop, cannot make it bigger without
scaling the whole desktop.

The maintainer asked (2026-11-08) for one text size, set in Settings, that
applies everywhere, with an example of every size the application uses so it
is clear what is being changed. It ships after 0.7.0.

## What Changes

- **Settings → Appearance gains Text size**: a percentage, 80 to 150% in 10%
  steps, 100% by default.
- **A preview under it** shows each text role the application uses (menus and
  buttons, labels, tool headings, axis and tick labels, readouts, small print),
  each drawn at the chosen size with its base size beside it, and updates as
  the setting moves.
- **It scales UI text only.** Menus, buttons, labels, tool text, axis labels
  and readouts follow it. Text that already sizes itself to its pane stays as
  it is: the tuner's large note name and its sharp and flat signs, and the
  Score View's notation.
- **Layouts grow with the text.** Rows and buttons that are a fixed height
  today, such as the Settings rows, the tuner's advanced rows and the title
  bar's buttons, take their height from the text size, so nothing is clipped
  at 150%.
- **Remembered like the theme.** It is saved in the settings file and travels
  with "appearance" in an exported settings bundle. A file without it loads at
  100%.

## Capabilities

### New Capabilities
- `text-size`: one text size for the whole application, its range, its
  preview, and what it does and does not scale.

### Modified Capabilities
None. `settings-transfer` already exports "appearance", which this change
widens without changing a requirement.

## Impact

- **Every tool and the shell** read font sizes through one place instead of
  writing them out. Each tool gets the size the way it gets the theme today:
  the shell pushes it, and no tool reaches back into the shell.
- **Layouts with fixed heights** in the shell, the Settings window and the
  tools are reworked to follow the text size.
- **Verification** is a full capture at 80%, 100% and 150%. The 100% captures
  must match today's approved references.
- **Not in this change:**
  - scaling notation or the tuner's note name;
  - a separate size per tool;
  - following the operating system's accessibility text size.
