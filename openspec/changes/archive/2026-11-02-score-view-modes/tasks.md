## 1. Layout support

- [x] 1.1 `System::bandTop`/`bandBottom`, set where `topOfStaff` accumulates.
      Test: consecutive bands touch, the first starts at 0, the last ends at
      `totalHeight`, and each band contains its own `topOfStaff`.
- [x] 1.2 `engraving::paginate(systems, pageHeight)` — JUCE-free, greedy, never
      splits a system, an oversize system on a page of its own (flagged).
      Tests: everything fits one page; an exact fit; a break; an oversize
      system; an empty layout; determinism.

## 2. Settings

- [x] 2.1 `ScoreViewSettings.{h,cpp}`: the view enum, the size ladder,
      `encode`/`decode`, nearest-step snapping, defaults on anything malformed.
      Tests for each.
- [x] 2.2 The Score View's `settingsVersion` in `BuiltInToolCatalog.h`;
      `captureSettings`/`applySettings` on `ScoreViewComponent`.

## 3. The views

- [x] 3.1 Vertical scroll: today's behaviour, behind the view switch.
- [x] 3.2 Horizontal scroll: unbounded width, horizontal scrolling only.
- [x] 3.3 Page: A4-proportion page fitted to the panel, paginated, one page
      shown, page edge, `‹ ›` and "Page n of m", Page Up/Down, Left/Right,
      Home/End; the oversize notice.
- [x] 3.4 Options menu: the three views, Enlarge, Shrink, Default size.

## 4. Enlarge and shrink

- [x] 4.1 `−` and `+` with the size between them in the title strip, disabled at
      the ends; Ctrl/Cmd `+`/`=`, `−`, `0`.

## 5. Keeping the place

- [x] 5.1 Record the first measure in view before a change of view or size, and
      reveal it after — per view, as design decision 6 says.

## 6. Verification

- [x] 6.1 `showView` for `page`, `vertical`, `horizontal`; an approved state and
      a capture surface for each, with questions on page edges, page turns and
      the scrollbar.
- [x] 6.2 Capture each view headlessly and look at it. That found the first
      Page design wrong: a whole page fitted into a short docked panel came out a
      few hundred pixels wide, one bar to a line, 68 pages for the sample. The
      page is now as wide as the panel and scrolls within itself (design
      decision 2, amended). Also checked by driving it: `›` turns to page 2,
      `+` re-paginates (4 → 6 pages) and keeps bar 19 at the top of the panel,
      and a named view resets the size, so a state photographs the same way
      whatever preceded it.
- [x] 6.3 `PracticeTakesTests`, `run_tests.py`, clang-format and clang-tidy.

## 7. Documentation

- [x] 7.1 `notation-engraving.md`: remove "Page presentation" from *What is not
      drawn*; describe the views and pagination.
- [x] 7.2 Two pages side by side: still no. With the page as wide as the panel,
      two would each be half as wide and hold half the music; it becomes worth
      it only with fit-page from #33, which sizes the page to the panel's
      height instead.
