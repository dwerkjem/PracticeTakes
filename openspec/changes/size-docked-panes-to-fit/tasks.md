## 1. Ask the tool what it shows when it is small

- [x] 1.1 A tool decides for itself from the size JUCE already gives it. The contract addition the proposal imagined was not needed: `resized()` and `paint()` both know the pane's size, so the shell was already passing it
- [x] 1.2 The shell passes the pane size and nothing else — it does not decide which parts of a tool matter
- [ ] 1.3 Confirm a tool with no compact form is still laid out entirely inside its pane rather than clipped
- [ ] 1.4 One shared rule for *which* compact shape applies — narrow-and-tall against short-and-wide — so three tools cannot disagree about what "small" means (design decision 6)

## 2. What each tool drops

- [x] 2.1 Tuner, compact: the note large, and whether it is sharp or flat
- [ ] 2.2 Tuner, graph: vertical when the pane is narrow, horizontal when it is short, with the scale coarsening as the space shrinks
- [ ] 2.3 Tuner, bar: a vertical or horizontal bar with the reading in the middle
- [ ] 2.4 Tuner, meter: the note, coloured by how close to in tune it is — a dial needs both dimensions and has neither (design decision 6)
- [ ] 2.5 Spectrogram: vertical or horizontal, no axis labels, no controls
- [ ] 2.6 Harmonic analyser: vertical or horizontal
- [ ] 2.7 Each tool returns to its full presentation when the pane grows, without losing analysis state

## 3. Chrome disappears, function does not (design decision 7)

- [ ] 3.1 Hide the mode chooser and the options button below the threshold
- [ ] 3.2 Right-click anywhere on a tool shows the same menu the options button does, at every size
- [ ] 3.3 Confirm nothing becomes unreachable by making a pane smaller

## 4. The floor

- [x] 4.1 `minimumHorizontalPaneSize` 480 → 180, below the tuner's 320 compact threshold so a pane at the floor shows a compact form rather than a squeezed full one
- [x] 4.2 `minimumVerticalPaneSize` 280 → 120, since the same arithmetic applies to stacked panes
- [x] 4.3 A split's minimum is its subtree's, not a flat constant. The flat one is why three tools failed and two never did: the outer split handed a nested split its ratio share while that nested split needed more
- [ ] 4.4 Confirm three and four docked tools fit at 980, 1280, and 1600

## 5. Surfaces for small panes (design decision 8)

- [ ] 5.1 Approved states putting a tool in a narrow pane and in a short pane at an ordinary window size
- [ ] 5.2 Cover each tuner display, the spectrogram, and the harmonic analyser in both shapes
- [ ] 5.3 Add them to the capture surface list, so the compact forms are photographed rather than merely written

## 6. Tests over the arithmetic

- [x] 6.1 The sizing rule is reachable without a display
- [x] 6.2 Two, three, and four panes never exceed the narrowest permitted window
- [x] 6.3 A regression test for the exact defect: 480 needed 1456px for three panes, which no legal window could give
- [ ] 6.4 The narrow-against-short rule, so the shape a tool picks is tested without a screenshot

## 7. Verification

- [ ] 7.1 `ctest` green
- [x] 7.2 Three tools whole and inside the window at 800×600, the size #150 reported as "entirely absent"
- [x] 7.3 Three tools whole at 640×480, which is below what the window manager permits and is where a layout that only just fits stops fitting
- [ ] 7.4 Every compact form captured in both shapes and looked at
- [ ] 7.5 Confirm the 800×600 capture is no longer byte-identical to `two-tools-split`, which is how run 6 found this
- [ ] 7.6 Re-judge the affected surfaces; their recorded verdicts describe the old behaviour
- [ ] 7.7 Confirm a floating tool is unaffected — it has no split to divide
