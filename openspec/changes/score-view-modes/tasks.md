## 1. Layout support

- [ ] 1.1 `System::bandTop`/`bandBottom`, set where `topOfStaff` accumulates.
      Test: consecutive bands touch, the first starts at 0, the last ends at
      `totalHeight`, and each band contains its own `topOfStaff`.
- [ ] 1.2 `engraving::paginate(systems, pageHeight)` — JUCE-free, greedy, never
      splits a system, an oversize system on a page of its own (flagged).
      Tests: everything fits one page; an exact fit; a break; an oversize
      system; an empty layout; determinism.

## 2. Settings

- [ ] 2.1 `ScoreViewSettings.{h,cpp}`: the view enum, the size ladder,
      `encode`/`decode`, nearest-step snapping, defaults on anything malformed.
      Tests for each.
- [ ] 2.2 The Score View's `settingsVersion` in `BuiltInToolCatalog.h`;
      `captureSettings`/`applySettings` on `ScoreViewComponent`.

## 3. The views

- [ ] 3.1 Vertical scroll: today's behaviour, behind the view switch.
- [ ] 3.2 Horizontal scroll: unbounded width, horizontal scrolling only.
- [ ] 3.3 Page: A4-proportion page fitted to the panel, paginated, one page
      shown, page edge, `‹ ›` and "Page n of m", Page Up/Down, Left/Right,
      Home/End; the oversize notice.
- [ ] 3.4 Options menu: the three views, Enlarge, Shrink, Default size.

## 4. Enlarge and shrink

- [ ] 4.1 `−` and `+` with the size between them in the title strip, disabled at
      the ends; Ctrl/Cmd `+`/`=`, `−`, `0`.

## 5. Keeping the place

- [ ] 5.1 Record the first measure in view before a change of view or size, and
      reveal it after — per view, as design decision 6 says.

## 6. Verification

- [ ] 6.1 `showView` for `page`, `vertical`, `horizontal`; an approved state and
      a capture surface for each, with questions on page edges, page turns and
      the scrollbar.
- [ ] 6.2 Capture each view headlessly and look at it.
- [ ] 6.3 `PracticeTakesTests`, `run_tests.py`, clang-format and clang-tidy.

## 7. Documentation

- [ ] 7.1 `notation-engraving.md`: remove "Page presentation" from *What is not
      drawn*; describe the views and pagination.
- [ ] 7.2 Answer the design's open question after implementation.
