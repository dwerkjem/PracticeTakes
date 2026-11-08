## Context

`engraving::layOutScore` turns a `Score` into systems of glyphs in staff
spaces. Each notehead carries its `score::NoteRef`, but nothing records where
a *tick* falls. Ticks between onsets, and rests, have no glyph to find. The
Score View lays out per view and size, paginates in Page view, and polls
`CurrentScore` at 2 Hz.

`transport::PracticeClock` (#36) notifies `ClockListener`s at 60 Hz with a
`ClockPosition`: ticks, bar, beat and state. Seeks and wraps arrive as events.
It is in `ToolServices::clock`.

## Goals / Non-Goals

**Goals:**
- From the clock's tick, the place on screen, with no layout pass.
- The sounding and upcoming notes of the shown parts, legible at any size.
- Following that a reader can predict, and that a reader can stop.
- Every rule here testable without a display.

**Non-Goals:**
- Judging what was played against the targets (#41, #43).
- Rehearsal marks and voice selection (see the proposal's Impact).
- Expanding repeats into a playback order. The clock plays the timeline as
  written, so the cursor does too.

## Decisions

### 1. Time anchors on each system, from the layout that already places notes

`System::timeAnchors` is a list of `{tick, x}` sorted by tick. Entries come
from every staff's main (non-grace) events at
`measure.start + event.onset`, at the event's justified x. Each bar adds one
more entry at its end, `measure.start + nominalDuration`, at its barline.

Staves share slots, so two staves agree on an onset's x, and duplicates are
dropped. A condensed multi-bar rest anchors only its start and end. Anchoring
from the layout rather than recomputing spacing means the cursor and the
noteheads can never disagree.

`System::staffParts` names the score part each staff draws. That is what a
highlight needs to tell the soprano's bar-3 note from the alto's: a
`NoteRef` names a measure, voice and event, but not a part.

*Rejected:* deriving x from proportional spacing (wrong after
justification), and anchoring only barlines (the cursor would cross a bar at
constant speed whatever its rhythm).

### 2. Between anchors, the cursor moves linearly

A tick between two anchors is placed by linear interpolation. The cursor then
moves at constant speed through a note's duration and reaches each onset
exactly as it sounds. A tick before the first anchor is clamped to it, and so
is one after the last. A tick in a bar a system doesn't hold isn't on that
system: the search finds the system whose anchors span the tick.

### 3. Targets: sounding, then the next *n* onsets, in the shown parts

At tick *t*, among the parts the Score View shows:
- **Sounding**: every note of a non-rest, non-grace event with
  `onset ≤ t < onset + duration`.
- **Upcoming**: the notes of the next *n* distinct onsets after *t*, where *n*
  is the look-ahead.

A note tied from an earlier note is not a new attack. It counts as sounding,
but never as upcoming. The look-ahead is a per-view setting from 0 to 4,
default 2. Two notes ahead is enough to prepare a breath or a shift without
cluttering a dense piano part.

### 4. Drawing: a band, filled heads and rings

The cursor is a translucent vertical band in the accent colour, a little over
a notehead wide, spanning the system's band. A sounding note is overdrawn with
an accent-coloured filled ellipse at 45% opacity, so the black head shows
through it. An upcoming note gets an accent ring. The marks sit over the
notation rather than replacing glyph colours, so the painter is unchanged,
and the marks read the same in both themes.

The clock's ticks arrive at 60 Hz. The Score View repaints only the bands of
the system the cursor left and the system it is in, never the whole canvas.

### 5. Following: page turns and system steps, never a crawl

Following is on by default, and acts only while the clock plays:
- **Page view**: when the cursor's system is on another page, turn to it, and
  scroll within a tall page so the system shows.
- **Vertical scroll**: when the cursor's system isn't wholly in view, scroll
  so it sits a quarter of the way down.
- **Horizontal scroll**: when the cursor passes 70% of the width, scroll so
  it is at 20%.

Each move happens at a predictable point (a new system, or a screen's worth)
rather than continuously, which is what "without disorienting jumps" means
for a reader.

Scrolling, turning a page or pressing a page key yourself suspends following.
While suspended and playing, a **Follow** button shows on the title strip.
Pressing it, or starting the clock again, resumes. Following can also be
turned off in the options menu, and that choice is remembered per view.

### 6. Clicking the music moves the clock

A click (not a drag) on a system seeks the clock to the onset at or before
the click's x, in that system. Clicking the notehead you want to start from
is how a practising musician expects to say "from here". **Go to bar…** in
the options menu takes a bar's printed number, or failing that its ordinal,
and seeks to its first tick. Every Score View, and any other clock reader,
follows, because the clock is shared.

### 7. Page view: fit width or fit page

Today a page is as wide as the panel and as tall as A4 makes it, scrolled
within when taller. **Fit page** sizes the page to the panel's height
instead, centred. The whole page is visible, smaller. **Fit width** is
today's behaviour and stays the default. The choice is per view and
remembered, like the view and size.

### 8. Settings

`score_view::Settings` gains three fields, with no new settings version.
Missing keys take their defaults, as `hidden` did:
- `lookAhead` (0–4, default 2);
- `follow` (default on);
- `pageFit` (width or page, default width).

## Risks / Trade-offs

- **Interpolation is linear in ticks, not in sound.** Through a tempo change
  inside a bar, the cursor's speed changes at the bar's next anchor, not at the
  exact tempo change. It is never more than one onset out, and it is exact at
  every onset.
- **A part with no notes in a long passage** has no sounding target, so
  nothing is marked, though the cursor still moves. That is correct: there
  is nothing to sing.
- **Repaint cost** is bounded by redrawing one or two system bands at 60 Hz
  while playing.
