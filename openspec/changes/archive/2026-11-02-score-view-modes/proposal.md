## Why

The Score View has one way to show a score: a column of systems scrolled up and
down, at one of three staff sizes chosen from a menu. That suits reading through
on a screen and nothing else. A musician expects to see a piece as it would sit
on the stand — page by page — and someone following along while playing wants
the music to move one way only, left to right. Both were asked for in #253,
together with resizing the music without opening a menu.

## What Changes

- **Three views, chosen from the tool's options menu**, per Score View instance:
  - **Page** (the default for a new Score View): fixed-size pages of the A4
    proportion, one shown at a time and fitted to the panel, turned with buttons
    and keys. A system is never split across pages.
  - **Vertical scroll**: what the view does today, unchanged.
  - **Horizontal scroll**: every bar on one system, scrolled left to right.
- **Enlarge and shrink**: `−` and `+` buttons on the Score View's title strip,
  stepping through finer staff sizes than today's three, with Ctrl/Cmd `+`, `−`
  and `0`, and menu entries for the same. The three fixed sizes are replaced.
- **Remembered per instance**: the view and the size are saved through the
  tool's `captureSettings`/`applySettings`, so each Score View comes back as it
  was left.
- **Switching keeps your place**: the bar in view before a change of view or
  size is in view after it.
- **One layout engine**: a view only changes the options the engine is given
  (width, and for Horizontal an unbounded one) and how its systems are arranged
  on screen. Pagination is a separate, JUCE-free step over the laid-out systems.
- **Every view is photographed**: an approved state and a capture surface for
  each, with questions about page edges, page turns and the scrollbar.

## Capabilities

### New Capabilities
- `score-view-modes`: the Score View's three views, its enlarge/shrink control,
  and the per-instance memory of both.

### Modified Capabilities
None. `score-notation-renderer` keeps its requirements; the "Page presentation"
row of `notation-engraving.md`'s *What is not drawn* is removed.

## Impact

- `src/features/score/ScoreViewComponent.{h,cpp}` — the views, the buttons, the
  keys, pagination on screen, keeping the place.
- `src/features/score/engraving/` — a `Pagination` step, and each `System`
  recording the vertical band it occupies.
- `src/features/score/ScoreViewSettings.{h,cpp}` — the settings codec, JUCE-free.
- `BuiltInToolCatalog.h` — a `settingsVersion` for the Score View.
- `ApprovedWindowStates` and `surfaces.py` — a state and a surface per view.
- Existing score-view captures change: a new Score View opens in Page view.
