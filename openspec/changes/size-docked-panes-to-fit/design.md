## Context

The arithmetic, from the source rather than from the symptom.

`WorkspaceSplitPane.h:20-22` sets `minimumHorizontalPaneSize = 480`,
`minimumVerticalPaneSize = 280`, `dividerThickness = 8`. The constructor hands
those to JUCE's `StretchableLayoutManager` as the minimum for each child:

    layoutManager.setItemLayout(0, minimumSize, -1.0, -ratio);
    layoutManager.setItemLayout(2, minimumSize, -1.0, -(1.0 - ratio));

A split holds two children, and a child may be another split
(`MainComponentWorkspaceLayout.cpp:116`), which is what lets the workspace
subdivide. So three docked tools are an outer split whose second child is an
inner split:

| | |
| --- | --- |
| inner split | 480 + 8 + 480 = 968 |
| outer split | 480 + 8 + 968 = **1456** |

`main.cpp:174` is `setResizeLimits(980, 600, 3200, 2200)`. 1456 is above the
maximum *minimum* the window can be, above the 1280 default, and above the 1600
a maximised window gets on a common display once the workspace's own padding is
taken. When the requested minimums exceed the space, `layOutComponents` lays
components out past the edge; nothing throws and nothing reports it.

That is the whole defect. It is not a rounding error at one size — it is a floor
that no legal window satisfies.

## Goals / Non-Goals

**Goals**

- A tool the user opened is always somewhere they can see it.
- Running out of room shows up as panes getting smaller, which is legible, not
  as a pane leaving the window, which is not.
- What a tool sacrifices when it gets small is decided by that tool.

**Non-Goals**

- Changing the tiling tree, the drag-and-drop, or saved layouts.
- Answering #113 for the tuner. This establishes that tools are asked.
- A general responsive-layout framework.

## Decisions

### 1. Share the space; do not reserve it

The floor drops to something a pane can actually be given, and the panes divide
what the window has in their configured ratio. This is what the design system's
own kit does — its panes are `flex: 1; min-width: 0`, which is CSS for "share
what there is and shrink below your content if you must".

*Alternative rejected: scroll the docked area horizontally once panes hit 480.*
It keeps every tool at a legible width, and it makes the workspace a thing you
navigate rather than a thing you glance at. The application is used while
holding an instrument.

*Alternative rejected: refuse to dock a third tool below some width.* It answers
a question nobody asked — the window can be resized after the dock, so the
constraint has to exist at resize time anyway, and then the refusal at dock time
is a second mechanism for the same problem.

### 2. The shell gives a size; the tool decides what to drop

A shell that decided what to drop would need to know what each tool is for. The
tuner at 260px should show the note and the direction of error; the spectrogram
should probably keep its plot and lose its axis labels; the harmonic analyser
might show three bars instead of eight. Those are three different answers and
only the tools know them.

This follows the existing shape of `ToolComponent`: `showView` already lets a
tool name its own presentations and the shell pass a name through without
understanding it.

### 3. Do not lower the floor before the compact forms exist

Lowering it alone would replace "the third tool is invisible" with "three tools
are illegible" — and run 6 already tagged the cramped case `ugly`, `illegible`,
`Cramped`. Both halves land together or the change makes the verdict worse while
appearing to fix the bug.

This is why the proposal's tasks put the tool work before the floor change, even
though the floor is the one-line fix.

### 4. A minimum that is a real minimum

Some floor is still needed: a pane of 3px is not a pane. The replacement floor
should be the width at which the *most compact* tool form is still legible,
which makes it a consequence of decision 2 rather than a number chosen up front.
It cannot be picked before the compact forms exist, which is another reason they
come first.

### 5. Test the arithmetic, not the screenshot

Whether three panes fit in 980px is arithmetic over the split tree, and belongs
in a unit test that fails on a build machine with no display. The captures are
how the *result* is judged — cramped, legible, ugly — which is what a person is
for.

`WorkspaceLayoutState` is already JUCE-free and unit-tested; the sizing rule
should be reachable the same way rather than living only inside a `Component`.

### 6. A compact form has two shapes, not one — decided 2026-08-11

A narrow pane and a short pane are different problems. Side-by-side splits make
panes tall and thin; stacked splits make them short and wide. A single compact
form would be wrong in one of them.

So each tool reads which dimension it is short of and draws along the other:

| Tool | Narrow and tall | Short and wide |
| --- | --- | --- |
| Tuner, graph | vertical trace, coarser scale | horizontal trace, coarser scale |
| Tuner, bar | vertical bar, reading in the middle | horizontal bar, reading in the middle |
| Tuner, meter | the note, coloured by how close it is | the same |
| Spectrogram | vertical | horizontal |
| Harmonic analyser | vertical | horizontal |

The scale coarsens with the size rather than staying fixed: a graph showing six
semitones over 90px of height is a line, not a graph.

The meter is the one that does not rotate. A dial needs width *and* height, and
below the threshold it has neither — so it becomes what it was really telling
you, which is the note and how close you are, carried by colour.

### 7. Below the threshold, the chrome goes too — decided 2026-08-11

A pane too small for a tool's full display is also too small to spend a header
row on a mode chooser and an options button. Both are hidden below the
threshold.

The options are still reachable: **right-click anywhere on the tool**. That is
the same menu the "..." button shows, from the surface that has the most room to
spare — the tool's own body. Nothing becomes unreachable by getting smaller,
which is the difference between hiding chrome and losing function.

### 8. Small panes need their own capture surfaces — decided 2026-08-11

The harness photographs surfaces at four geometries by resizing the *window*.
That reaches a small window, not a small *pane*: three tools in a 1280 window
give panes of about 400px, which is above the threshold, so the compact forms
would never appear in a capture.

New approved states put a tool in a pane that is narrow, and in one that is
short, at an ordinary window size. Without them every compact form here is
code no screenshot has ever shown, which is the position the audio callback was
in before the sanitizer work.

## Risks / Trade-offs

- **Every tool has to answer.** Three tools today, and each needs a considered
  compact form. The alternative is a shell that guesses, which produces the
  clipped headings this change exists to remove.
- **The capture surfaces move.** `all-tools-docked` and `three-tools-tabbed`
  change appearance, so their recorded verdicts describe the old behaviour and
  should be re-judged rather than carried forward.
- **Compact forms are a new place for bugs**, reachable only at sizes nobody
  looks at by habit. Mitigated by the capture harness covering `tiny` (640x480)
  already.
- **#113 overlaps.** It asks the same question for the tuner alone. Whichever
  lands first should answer it once.

## Migration Plan

1. The contract: how a tool is asked for a compact form, defaulting to "no
   compact form", so nothing changes until a tool opts in.
2. Compact forms per tool, tuner first, with #113's judgement.
3. The floor, once there is a compact form to justify its new value.
4. Unit tests over the split arithmetic at 980, 1280, and 1600 for two, three,
   and four tools.
5. Re-capture the affected surfaces and re-judge them.

Steps 1 and 2 are safe on their own: a tool that never gets a small pane never
draws small.

## Open Questions — resolved

- **Where is the threshold — per tool, or one number?** One number:
  `CompactPresentation.h`'s `fullWidth`/`fullHeight` (320×220), shared by all
  three tools and by `DockedToolPanel`'s own chrome-hiding. Per-tool was
  rejected implicitly by never being built — one shared rule is what keeps two
  tools at the same pane size from disagreeing about which of them is "small".

- **What should a tabbed group do? — decided 2026-08-13.** Give the tab bar's
  own depth a place in the arithmetic, on whichever axis it actually spends:
  `WorkspaceSplitPane::minimumWidthOf`/`minimumHeightOf` now recognise a
  `juce::TabbedComponent` child and add its `getTabBarDepth()` to the pane
  floor on that axis. This was not the same defect #150 was — JUCE's
  `TabbedButtonBar` already degrades a tab bar that runs out of room on its own
  axis by shrinking tabs and folding the rest behind an overflow button, rather
  than drawing past the edge — but it was the *same shape* of defect: a
  composite component's real minimum, quietly larger than the flat constant
  standing in for it, on the axis the bar's chrome actually costs (height, for
  the `TabsAtTop` this workspace builds). Read from the component
  (`getTabBarDepth()`) rather than duplicated as a second constant that could
  drift from `MainComponentWorkspaceLayout.cpp`'s `setTabBarDepth(38)`.

- **Should the vertical floor of 280 move too?** It already had — 280 → 120,
  done alongside the horizontal floor in task 4.2, on the same reasoning: the
  same arithmetic applies to stacked panes, and there was no reason to leave
  one axis fixed while lowering the other.
