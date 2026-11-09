## Context

- **Hard-coded sizes:** text is drawn at sizes written into the code,
  `juce::FontOptions(12.0f)` and the like: 72 calls in 19 files. They are in
  the shell windows, every tool, `MiniStaff`, the tuner's drawing and the
  score's glyph painter.
- **Widget fonts:** JUCE's own widgets (buttons, labels, combo boxes, menus)
  take their fonts from the look-and-feel. `MainComponent` owns
  `AppLookAndFeel` and sets it on itself, its menus, alerts and docks.
- **The pattern to copy:** a tool learns the theme because the shell calls
  `setTheme` on it. Text size should reach tools the same way.

## Goals / Non-Goals

**Goals:**
- One setting scales every piece of UI text.
- At 100% nothing moves: the 100% captures equal today's references.
- At 150% nothing is clipped or overlaps.
- The rule (range, steps, rounding, the role table) is JUCE-free and tested.

**Non-Goals:**
- Scaling notation, the tuner's note name or its signs, which already size
  themselves to their pane.
- A size per tool.
- Following the operating system's text-size setting.

## Decisions

### 1. A scale, not a point size
Text size is a percentage, 80 to 150% in 10% steps. A single point size
cannot describe an application that uses six sizes on purpose: a scale keeps
their proportions.
- **The rule:** `appearance::TextScale` is JUCE-free. It holds the bounds and
  the step, snaps a stored value to the nearest step, and turns a base size
  into a scaled one, rounded to half a point so text does not shimmer between
  sizes.

### 2. Text roles, declared once
- **The table:** each kind of text the application draws is a role in one
  JUCE-free table, with its base size and a name the preview shows. The roles
  are menus and buttons, labels, tool headings, axis and tick labels,
  readouts, and small print.
- **Call sites ask by role:** a call that wrote `FontOptions(11.0f)` asks for
  `TextRole::axis` instead. That is how the preview can show "every size we
  use", and why a new size cannot appear without a name.
- **Exemptions stay explicit:** the few sizes that follow their pane (the
  tuner's note name and signs, the notation) keep computing their own size and
  are not roles.
- **One role per size in use (decided in implementation):** the code drew
  sixteen distinct sizes, from 9 to 34 points, not six. Six broad roles would
  have moved text by a point or two at 100%, against the goal that 100%
  changes nothing. So the table has a named role for each size in use, plus
  JUCE's own sizes for menus (17), buttons and choices (16) and labels (15),
  each named for what is drawn at it. Two roles may share a size
  (subheadings and buttons, both 16). Folding sizes together is a later,
  visible design choice, not something to slip into this change.
- **Labels set base sizes:** a `juce::Label` is given its role's *base* size,
  and the look-and-feel scales every label's font. Text a component paints
  itself is drawn at the scaled size. One rule each way, so nothing is scaled
  twice.

### 3. How it reaches each component
- **Widgets:** `AppLookAndFeel` holds the current scale and applies it in its
  font overrides (`getTextButtonFont`, `getLabelFont`, `getComboBoxFont`,
  `getPopupMenuFont`, tab and alert fonts).
- **Tools:** the shell pushes the scale with `ToolComponent::setTextScale`,
  as it pushes the theme. A tool keeps it and passes it to its drawing
  helpers. Nothing global is added, and no tool reads application state any
  other way.
- **Shell windows** read it from `MainComponent`, which owns it.

### 4. Layout heights follow the text
- **Heights from the role:** a row or button with a fixed height today takes
  it from its role's scaled size plus padding.
- **Applying a change:** changing the text size calls `resized()` on the main
  window, the Settings window and every tool.
- **Where text can't fit:** a tool with too little room already switches to
  its compact form at a size threshold, and that threshold follows the scale
  too.

### 5. Stored like the theme
- **The key:** `appearance.textScale` in the settings file, read through the
  rule so a missing or out-of-range value loads at 100%.
- **Bundles:** it is part of "appearance" in an exported bundle, as the theme
  is. A bundle without it imports at 100%.

## Risks / Trade-offs

- **Layouts at 150%:** many layouts were tuned by eye at one size, so a full
  capture at 80%, 100% and 150% is part of the change, not an afterthought.
- **Converting 72 call sites** is mechanical but wide. Doing it role by role,
  one commit per area (shell, Settings, each tool), keeps each step
  reviewable, and the 100% capture shows anything that moved.
