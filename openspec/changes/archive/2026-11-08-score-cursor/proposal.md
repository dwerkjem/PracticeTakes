## Why

The Score View draws the score. The practice clock (#36) now knows where in
the score you are. Nothing connects the two: you can play a piece with the
metronome and see nothing move on the page.

Issue #33 asks for the connection:
- a cursor at the current position;
- the current notes, and the next few, marked;
- the view following the clock without disorienting jumps, until you scroll
  it yourself;
- navigation by bar;
- zoom to fit a page or the width.

Its earlier half, three views, zoom steps and part selection, shipped as
`score-view-modes` and `score-part-selection`. This is the half the clock
unblocked. It is also what #40 (takes), #41 (live feedback on the score) and
#43 (timing judgement) will draw on top of: a way to find, for any tick,
where it is drawn and which notes are due.

## What Changes

- **The layout records where time is.** Each laid-out system carries its
  time anchors (tick → x, from every note's onset and every barline) and
  which part each staff belongs to. The engraving layer stays JUCE-free.
- **A JUCE-free `ScoreCursor`:**
  - from a tick, the system and x it is drawn at, moving smoothly between
    onsets;
  - from an x, the onset to seek to;
  - the notes sounding at a tick, and the notes of the next *n* onsets, in
    the parts the view shows.
- **The Score View reads the practice clock** through `ToolServices`. It:
  - draws a cursor across the system the clock is in;
  - marks the sounding notes;
  - rings the notes coming next, up to a set number of onsets ahead (look
    ahead: none to four, two by default).
- **It follows the clock while it plays**: it turns the page in Page view,
  steps a system at a time in Vertical scroll, and moves a screen at a time in
  Horizontal scroll. Scrolling or turning pages yourself stops the following,
  until you press **Follow** or play again.
- **Clicking a note moves the clock there**, and **Go to bar…** moves it to a
  bar by its printed number.
- **Page view fits the page or the width.** Fit width is how it works today.
  Fit page shows the whole page in the panel.

## Capabilities

### New Capabilities

- `score-cursor`: following the practice clock in the Score View.

### Modified Capabilities

None. `score-view-modes` gains a fit choice only within Page view, specified
here.

## Impact

- `src/features/score/engraving/`: `ScoreLayout.h` (time anchors and the
  staff-to-part map), `ScoreLayoutEngine.cpp`, and the new `ScoreCursor.{h,cpp}`.
- `src/features/score/ScoreViewComponent.*`, `ScoreViewSettings.*` (look
  ahead, follow, page fit), and `BuiltInTools.cpp` (the factory passes the
  clock).
- An approved state and capture surface with the cursor drawn.
- The wiki's Scores page.
- Out of scope, and why:
  - **Rehearsal marks.** The importer drops them today
    (`MusicXmlSubset.cpp`), so there is nothing to navigate to. Keeping them
    is an importer change, filed as a follow-up.
  - **Choosing one voice within a part.** Part selection already lets a
    singer pick their line from a multipart score, which is #33's acceptance
    criterion. Two voices sharing a staff (SATB on two staves) is filed as a
    follow-up.
