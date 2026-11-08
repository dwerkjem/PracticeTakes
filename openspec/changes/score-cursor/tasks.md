## 1. Layout and cursor rules, JUCE-free

- [x] 1.1 `System::timeAnchors` and `System::staffParts`, from the layout
      engine: every staff's main onsets and every bar's end, deduplicated and
      sorted; a condensed rest's start and end.
- [x] 1.2 `engraving/ScoreCursor.{h,cpp}`:
      - tick → (system, x) by interpolation;
      - x → onset tick on a system;
      - the sounding and upcoming targets in the shown parts.
- [x] 1.3 Tests in `src/tests/features/score/engraving/ScoreCursorTests.cpp`:
      anchors match noteheads, interpolation through a half note, a tied note
      not upcoming, hidden parts, two staves sharing an onset, and a
      multi-bar rest.

## 2. Settings

- [ ] 2.1 `lookAhead`, `follow` and `pageFit` in `score_view::Settings`, with
      codec tests: defaults when missing, clamped when out of range.

## 3. The Score View

- [ ] 3.1 The clock in the factory, through `ToolServices`. The view becomes a
      `ClockListener`.
- [ ] 3.2 The cursor band and the sounding and upcoming marks, repainting only
      the systems that change.
- [ ] 3.3 Following, with manual suspension and the Follow button.
- [ ] 3.4 Click to seek, and Go to bar.
- [ ] 3.5 Fit page and fit width in Page view; the options menu entries.

## 4. Verification and docs

- [ ] 4.1 An approved state with the clock positioned mid-score (paused, so
      the picture is stable), and its capture surface. Read the pictures.
- [ ] 4.2 The wiki's Scores page and the Features row, and follow-up issues for
      rehearsal marks and voice selection.
