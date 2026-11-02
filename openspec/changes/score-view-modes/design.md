## Context

`ScoreViewComponent` lays the open score out at the viewport's width
(`LayoutOptions::availableWidth`) and stacks the systems on one tall canvas in a
`juce::Viewport` with a vertical scrollbar. Its size is one of three staff
spaces in pixels — 5.2, 7.0 and 9.4 — chosen from the options menu and not
saved. Since `engraving-polish` 2.4 (#252), systems are different heights:
`topOfStaff` accumulates and `totalHeight` is the sum.

The shell already persists a tool's settings per instance
(`ToolComponent::captureSettings`/`applySettings`, versioned by the tool's
`ToolDefinition::settingsVersion`), and the capture harness can open a tool in
a named view (`ToolComponent::showView`). The tuner uses both.

## Goals / Non-Goals

**Goals:** the three views of #253; enlarge/shrink by button, key and menu; both
remembered per instance; the bar in view kept in view across a change.

**Non-Goals:** justification (systems stretched to the page width — that is
`engraving-polish` 3.1, and a page holds ragged systems until it lands);
fit-width and fit-page, page/measure navigation and the following cursor (#33);
a margin repeating clef, key and part names in Horizontal scroll (later, per
#253); printing.

## Decisions

### 1. A view is options plus arrangement; the engine does not know about pages

Vertical gives the engine the viewport's width, as now. Page gives it the page's
inner width. Horizontal gives it an unbounded width, so no system ever breaks.
Pages are then cut from the laid-out systems by a separate JUCE-free function,
`engraving::paginate`, from each system's vertical band. Keeping pagination out
of the engine keeps every layout test, and the single-part identity guarantee,
true of every view.

Each `System` therefore records its band — `bandTop` and `bandBottom`, in staff
spaces from the top of the layout, from the same accumulation that places
`topOfStaff`. Consecutive bands touch, and the last one ends at `totalHeight`.

### 2. Page: one page at a time, A4-shaped, fitted to the panel

A page has the proportion of A4 (1 : √2), as large as fits the panel, with a
margin inside it and a visible edge. Its size in pixels follows the panel; the
staff size follows the zoom, so enlarging puts fewer systems on a page, as
#253 asks. One page is shown at a time rather than a scrolling column of pages:
the point of the view is the page turn, and a page that scrolls is a tall
canvas again.

Systems are assigned to pages greedily, in order, never split. A system taller
than a page — a large score at a large size — gets a page of its own and is
clipped at the page's foot, and the view says so in its notice strip rather
than shrinking the music behind the user's back.

Page turns: `‹` and `›` buttons and "Page *n* of *m*" in the title strip;
Page Up/Down, Left/Right and Home/End when the Score View has focus.

### 3. Horizontal: one system, and only the scrollbar it needs

`availableWidth` is effectively unbounded, so the packer never starts a second
system. The canvas is as wide as that one system, scrolled horizontally. A
vertical scrollbar appears only if the system is taller than the panel, which
at ordinary sizes it is not.

### 4. Zoom is a step through a fixed ladder of staff sizes

Staff space in pixels, smallest to largest:
4.0, 4.6, 5.2, 6.0, 7.0, 8.2, 9.4, 11.0, 13.0. The default is 7.0, today's
Medium; today's Small and Large are on the ladder, so nobody's preferred size
disappears. `−` and `+` step down and up and disable at the ends; the label
between them shows the size as a percentage of the default. Ctrl/Cmd `+`
(and `=`), `−` and `0` do the same when the Score View has focus, and the
options menu offers Enlarge, Shrink and Default size beside the three views.

A ladder rather than a continuous factor because each step has to change the
layout noticeably — a system break moving is the visible effect — and because a
saved size has to be one a later version still offers.

### 5. Settings: a small versioned text payload

`view=page;size=7` — the view's name and the staff size, in a
`ToolSettingsPayload` at version 1, with the Score View's `settingsVersion` set
to match. The codec is JUCE-free (`ScoreViewSettings.{h,cpp}`) and tested:
unknown views fall back to Page, a size not on the ladder snaps to the nearest
step, a malformed payload decodes to the defaults. Today's unsaved Small,
Medium and Large have nothing to migrate.

### 6. Keeping the place

Before a change, the view records the first measure index it is showing: the
first system whose band reaches the top of the viewport (Vertical), the first
system on the current page (Page), or the measure under the left edge
(Horizontal, from the x of its first notehead or barline). After relaying out,
it reveals that measure: scrolls its system to the top, turns to its page, or
scrolls its x to the left edge.

### 7. Each view is a named view for the harness

`showView("page" | "vertical" | "horizontal")` switches to that view, so an
approved state can open the sample score in each. The existing score-view
states open in the default, Page.

## Risks / Trade-offs

- **Ragged pages until justification lands.** Page view looks less like print
  than it will. Accepted: #253 says so, and the page structure does not change
  when 3.1 lands.
- **Every existing score-view capture changes**, because a new Score View
  opens in Page view. Their questions still apply; the images are re-reviewed
  once.
- **A very long Horizontal canvas.** Several hundred bars at a large size is a
  canvas tens of thousands of pixels wide. JUCE clips painting to the visible
  region, but the layout of every glyph is still held. Acceptable for the
  scores in the corpus; revisited if a real score makes it slow.

## Open Questions

- Should Page view show two pages side by side when the panel is wide enough?
  Not in this change; the arrangement is one function and can grow.
