## Why

Dock a third tool and one of them is not on screen.

`WorkspaceSplitPane` gives every pane a hard floor: `minimumHorizontalPaneSize`
is 480 and `dividerThickness` is 8. Three docked tools are nested splits, so the
workspace demands `480 x 3 + 8 x 2 = 1456px` before it will lay out at all.
`main.cpp:174` lets the window go down to 980px wide. The floor exceeds every
legal window width, and JUCE's layout manager resolves that by running the last
pane past the right edge.

Manual verification run 6 found it at all three capture sizes, and the numbers
say the same thing the tester did:

| Window | What you see |
| --- | --- |
| 1600x900 maximised | the harmonic analyser is cut, its heading reads "Listening for a sustained **pit**" |
| 1280x800 default | title truncated, two of five harmonic labels on screen, the plot a slice |
| 800x600 | the harmonic analyser is **entirely absent** |

The capture at 800x600 came out byte-identical to `two-tools-split` at the same
size. Three tools and two tools produced the same pixels, which is only possible
if the third was never drawn.

The surface was failed on *looks-well-presented at every size* and tagged
`ugly`, `illegible`, `Cramped`. Nothing in the application said anything was
wrong: no warning, no scroll bar, no indication that a tool the user opened is
off the edge of the window.

## What Changes

- **Panes divide the width available instead of overflowing it.** The floor
  drops far enough that any number of docked tools fits any legal window, and
  the space is shared rather than reserved.
- **A tool draws a compact form when its pane is narrow.** Shrinking alone
  converts "the third tool is invisible" into "three tools are illegible", which
  is the same verdict from the other direction. Below a threshold a tool shows
  the least that is still useful: for the tuner, the note and whether you are
  sharp or flat.
- **The compact form is the tool's decision, not the shell's.** The shell gives
  a pane its size; what to drop at that size is a question only the tool can
  answer, and the answer differs per tool.

## Non-goals

- **No change to the tiling model.** Same tree, same drag-and-drop, same saved
  layouts. This is about how a split divides what it has.
- **No scrolling docked area.** Considered and rejected in the design; a
  workspace you scroll sideways is the wrong shape for something glanced at
  mid-practice.
- **No limit on how many tools may dock.** Refusing the third dock was the third
  option in #150 and it answers the wrong question: the window can still be
  resized afterwards.
- **Not the tuner's compact form itself.** #113 asks that question for the
  tuner specifically and should answer it; this change establishes that tools
  are asked, and what the shell guarantees them.

## Capabilities

### New Capabilities

- `workspace-pane-sizing`: how a split divides the space it has, what a pane is
  guaranteed, and how a tool is told it must draw small. Neither
  `workspace-layout-restoration` nor `workspace-tab-drag` covers sizing — they
  cover what is restored and what dragging does.

## Impact

- `src/application/shell/ui/workspace/components/WorkspaceSplitPane.h` — the
  floor, and how the layout manager is configured.
- `src/application/tools/ToolComponent.h` — how a tool is asked for its compact
  form.
- `src/features/analysis/*` — each tool decides what it drops.
- `src/tests/application/shell/ui/workspace/` — the sizing rule is arithmetic
  and belongs in a unit test, not a screenshot.
- **The capture surfaces move.** `all-tools-docked` and `three-tools-tabbed` are
  what this changes; their prior verdicts describe the old behaviour.
- **Not affected:** the audio path, the tool registry, saved workspaces, and
  every tool that is wide enough to draw normally.
