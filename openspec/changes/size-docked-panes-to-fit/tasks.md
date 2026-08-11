## 1. Ask the tool what it shows when it is small

- [ ] 1.1 Extend `ToolComponent` so a tool can declare a compact presentation, defaulting to none, so no tool changes until it opts in (design decision 2)
- [ ] 1.2 The shell passes the pane size and nothing else — it does not decide which parts of a tool matter
- [ ] 1.3 Confirm a tool with no compact form is still laid out entirely inside its pane rather than clipped

## 2. What each tool drops

- [ ] 2.1 Tuner: the note and whether it is sharp or flat, which is what #113 asks and what makes the tool usable in a narrow pane
- [ ] 2.2 Spectrogram: decide between keeping the plot without axis labels and something else; record the reasoning
- [ ] 2.3 Harmonic analyser: fewer harmonics, or a summary; record the reasoning
- [ ] 2.4 Each tool returns to its full presentation when the pane grows, without losing analysis state

## 3. The floor

- [ ] 3.1 Replace `minimumHorizontalPaneSize` with a value derived from the most compact form that is still legible (design decision 4) — **not before section 2**, or the fix trades an invisible tool for three illegible ones (design decision 3)
- [ ] 3.2 Decide whether `minimumVerticalPaneSize` moves with it; the same arithmetic applies to stacked panes
- [ ] 3.3 Confirm three docked tools fit at 980, 1280, and 1600, and that four do too

## 4. Tests over the arithmetic

- [ ] 4.1 Make the sizing rule reachable without a display, the way `WorkspaceLayoutState` already is
- [ ] 4.2 Unit tests: total width demanded by two, three, and four panes never exceeds the window at the minimum permitted width
- [ ] 4.3 A regression test for the exact defect: three panes at 800x600 must not produce the same layout as two

## 5. Verification

- [ ] 5.1 `ctest` green
- [ ] 5.2 Capture `all-tools-docked` and `three-tools-tabbed` at every geometry and confirm the third tool is present and whole at each
- [ ] 5.3 Confirm the 800x600 capture is no longer byte-identical to `two-tools-split`, which is how run 6 found this
- [ ] 5.4 Re-judge the affected surfaces; their recorded verdicts describe the old behaviour
- [ ] 5.5 Confirm a floating tool is unaffected — it has no split to divide
