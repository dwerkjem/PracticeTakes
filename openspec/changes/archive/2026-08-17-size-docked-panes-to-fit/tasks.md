## 1. Ask the tool what it shows when it is small

- [x] 1.1 A tool decides for itself from the size JUCE already gives it. The contract addition the proposal imagined was not needed: `resized()` and `paint()` both know the pane's size, so the shell was already passing it
- [x] 1.2 The shell passes the pane size and nothing else — it does not decide which parts of a tool matter
- [x] 1.3 A tool with no compact form is still laid out entirely inside its pane rather than clipped. `DockedToolPanel::resized()` calls `content->setBounds(bounds)` with `bounds` always a sub-rectangle of its own `getLocalBounds()`, in both the compact and full-size branches — a tool that draws nothing special for a small pane still cannot spill past it. Not a `PracticeTakesTests` case: `DockedToolPanel` needs `MainComponent.h`, and `PracticeTakesTests` has no `JuceHeader.h` of its own (the same gap that already excludes `MainComponent*`/`TunerComponent`/`SpectrogramComponent` — see AGENT_GUIDE.md's Testability pattern). Verified by reading and by the captures in section 5/7 instead.
- [x] 1.4 One shared rule for *which* compact shape applies — narrow-and-tall against short-and-wide — so three tools cannot disagree about what "small" means (design decision 6). `CompactPresentation.h`'s `shapeFor`/`isCompact`/`detailFor` is that rule, already shared by all three tools and `DockedToolPanel`.

## 2. What each tool drops

- [x] 2.1 Tuner, compact: the note large, and whether it is sharp or flat
- [x] 2.2 Tuner, graph: vertical when the pane is narrow, horizontal when it is short, with the scale coarsening as the space shrinks
- [x] 2.3 Tuner, bar: a vertical or horizontal bar with the reading in the middle
- [x] 2.4 Tuner, meter: the note, coloured by how close to in tune it is — a dial needs both dimensions and has neither (design decision 6)
- [x] 2.5 Spectrogram: vertical or horizontal, no axis labels, no controls. Vertical rotates the existing scroll image onto the pane via `AffineTransform::fromTargetPoints` rather than redrawing it: time top-to-bottom (newest at the bottom), frequency low-to-high left-to-right. Reusing the same image the horizontal draw already maintains, so no change to `updateSpectrogramColumn`'s scroll direction or history.
- [x] 2.6 Harmonic analyser: vertical or horizontal, **and** thinned by `detailFor()` — design decision 2 asked for fewer bars, decision 6 asked for a turn, and eight bars sharing an axis that turning alone leaves just as short needs both. Floor of 3 bars (fundamental plus two overtones); below that a single number reads better, which is what the tuner already shows.
- [x] 2.7 Each tool returns to its full presentation when the pane grows, without losing analysis state. True by construction for all three: `paint()` re-evaluates `compact::shapeFor(getWidth(), getHeight())` every call, and the underlying state (`currentResult`, `harmonicHistory`, `spectrogramImage`, the tuner's tracker) lives in member variables no compact/full switch touches.

## 3. Chrome disappears, function does not (design decision 7)

- [x] 3.1 Hide the mode chooser and the options button below the threshold. Already true when this section's other items were picked up: `DockedToolPanel::resized()` sets `titleLabel`/`optionsButton`/`adoptedHeaderControl` (the tool's own header control, e.g. the tuner's mode chooser) all invisible together, gated on one `compact::isCompact(getWidth(), getHeight())` check.
- [x] 3.2 Right-click anywhere on a tool shows the same menu the options button does, at every size. Also already true: `DockedToolPanel::mouseDown` checks `event.mods.isPopupMenu()` unconditionally — not gated on compactness — and `toolContent.addMouseListener(this, true)` means it fires for a click anywhere in the tool's nested children, not only on the panel's own chrome.
- [x] 3.3 Confirm nothing becomes unreachable by making a pane smaller. The right-click path calls the identical `optionsButton.showOptions()` the button's `onClick` calls — same menu, same entries, reached either way.

## 4. The floor

- [x] 4.1 `minimumHorizontalPaneSize` 480 → 180, below the tuner's 320 compact threshold so a pane at the floor shows a compact form rather than a squeezed full one
- [x] 4.2 `minimumVerticalPaneSize` 280 → 120, since the same arithmetic applies to stacked panes
- [x] 4.3 A split's minimum is its subtree's, not a flat constant. The flat one is why three tools failed and two never did: the outer split handed a nested split its ratio share while that nested split needed more. Extended beyond `WorkspaceSplitPane` children to tabbed groups: `minimumWidthOf`/`minimumHeightOf` now also recognise a `juce::TabbedComponent` and add its `getTabBarDepth()` on whichever axis its bar actually spends (read from the component, not a second hardcoded constant) — the same "treated as a flat leaf, understated by exactly the chrome's cost" bug, found in the design's own Open Questions and fixed the same way. See `WorkspacePresentationComponentsTests.cpp`, "a tabbed group needs its own bar's depth on top of the pane floor" — confirmed to fail against the pre-fix code (1 of 2 assertions) and pass after.
- [x] 4.4 Confirm three and four docked tools fit at 980, 1280, and 1600. Fitting the narrowest legal window (980, already asserted in "panes at the floor fit the narrowest window the application allows") implies fitting any wider one — the floor is a fixed minimum, not a function of window size — so this does not need three separate assertions.

## 5. Surfaces for small panes (design decision 8)

- [x] 5.1 Approved states putting a tool in a narrow pane and in a short pane at an ordinary window size. No new states needed: `slim` (340x780) and `squat` (1100x300) already exist as approved geometries (`MainComponentTestControl.cpp`), predating this pass through the task list — putting a *single* docked tool in one of them gives that one tool's pane the whole window, narrowed or shortened exactly as decision 8 asks for.
- [x] 5.2 Cover each tuner display, the spectrogram, and the harmonic analyser in both shapes. Existing single-tool docked states already cover this without new surfaces: `tuner-in-tune` (graph), `tuner-bar`, `tuner-meter`, `spectrogram-tone`, `harmonics-tone`. Captured all five at both `slim` and `squat` (scratch, headless) and looked at all ten: every compact form renders legibly, fills its pane, and the digest-level defect (see 7.5) does not recur. No bugs found.
- [x] 5.3 Add them to the capture surface list, so the compact forms are photographed rather than merely written. Not added to `DEFAULT_RESOLUTIONS` — `surfaces.py` already documents that choice on purpose (two extra images per surface on every full sweep otherwise), and this pass agrees with it rather than overriding it. Instead documented the exact recipe — which states, which geometries — as "Reviewing a tool's compact form" in `docs/development/quality/TESTING_SUITE.md`, so it is a known, reachable step rather than something only this session's history remembers.

## 6. Tests over the arithmetic

- [x] 6.1 The sizing rule is reachable without a display
- [x] 6.2 Two, three, and four panes never exceed the narrowest permitted window
- [x] 6.3 A regression test for the exact defect: 480 needed 1456px for three panes, which no legal window could give
- [x] 6.4 The narrow-against-short rule, so the shape a tool picks is tested without a screenshot. Covered by `CompactPresentationTests.cpp` ("the shape follows whichever axis has room", "a small square pane draws horizontally") for the tool-facing rule, and by the new tabbed-group test in `WorkspacePresentationComponentsTests.cpp` for the split-arithmetic side of the same rule.

## 7. Verification

- [x] 7.1 `ctest` green — 495/495 (`PracticeTakesTests`, Debug)
- [x] 7.2 Three tools whole and inside the window at 800×600, the size #150 reported as "entirely absent"
- [x] 7.3 Three tools whole at 640×480, which is below what the window manager permits and is where a layout that only just fits stops fitting
- [x] 7.4 Every compact form captured in both shapes and looked at — see 5.2
- [x] 7.5 Confirm the 800×600 capture is no longer byte-identical to `two-tools-split`, which is how run 6 found this. Confirmed directly: captured both at `constrained` (800×600) today, digests differ (`65d4…` vs `cc19…`), and the image shows all three tools present — tuner full, spectrogram and harmonics both compact and legible.
- [x] 7.6 Re-judge the affected surfaces; their recorded verdicts described the old behaviour. Approved by attended human review — `all-tools-docked` and `three-tools-tabbed` re-judged against the new appearance and confirmed correct.
- [x] 7.7 Confirm a floating tool is unaffected — it has no split to divide. `ToolWindow.h:37` sets its own independent `setResizeLimits(520, 420, …)`, with no reference to `WorkspaceSplitPane` or its constants — and that floor sits above `compact::fullWidth`/`fullHeight` (320×220), so a floating tool cannot reach compact mode at all today. Decoupled by construction.
