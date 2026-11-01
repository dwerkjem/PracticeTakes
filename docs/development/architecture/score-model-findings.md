# What the score model got wrong

`src/platform/score/` was built by `musicxml-import` and tested only by an
importer. Issue #187 asked for the cheapest possible test of its assumptions: a
real consumer in the running application, and **a written list of what consuming
it revealed**. That list is this file, and it is the actual deliverable of the
`score-target-note-tool` change — the tool it added is the instrument, not the
result.

The consumer is small on purpose: `selectTargetNote` walks a `Score` to its
first pitched note and describes it. Everything below came out of writing about
a hundred lines against a model that until then had only ever been written
*to*.

Each entry says what the friction was and what should happen about it.

**A second consumer has since read the model**, and this file was extended
rather than replaced. `score-notation-renderer` (#32) draws the score, which
asks far more of it than describing one note did: every note's staff position,
every duration's written value, every accidental in context. Findings 10 and 11
came out of that, and finding 7 was resolved by it. What drawing the model
revealed is in [*What drawing it revealed*](#what-drawing-it-revealed) below.

## What survived contact

Worth stating first, because a findings document that lists only problems
misreports the result. Three decisions were load-bearing and held.

- **`std::shared_ptr<const Score>` is the right ownership.** A reader takes a
  copy, keeps reading it while a different score is opened, and needs no lock
  for it. The tool relies on this every two seconds and it required no care at
  all at the call site.
- **Pointer identity is a sufficient change check.** A consumer that holds the
  score it last rendered can detect a new one by comparing pointers, with no
  generation counter, no version field, and no callback — because holding the
  old score is what stops its address being reused. One `if`.
- **`ToolServices` cost one field.** Its own comment claimed that adding a
  service later would be "one field here instead of a signature change at every
  factory". That was tested for the first time here and was exactly true: one
  field, one line at each of the two construction sites, and not one existing
  tool factory changed.

## What drawing it revealed

Added by `score-notation-renderer`. The headline is the one worth stating
plainly: **the model was built for a renderer and it works.**

- **Keeping the spelling *and* the sounding number was the right call, and
  `Pitch.h` was right about why.** Its own comment says "the renderer (#32)
  needs step/alter/octave to pick a staff line and an accidental". That is
  exactly what `StaffPosition` does, and nothing else in the model was needed
  to do it: a clef is a sign on a line, a pitch is a spelling, and the position
  is a subtraction.
- **Invariant 10 cost nothing and bought the layout.** Having nowhere to put a
  coordinate meant there was never a question about where one came from. The
  layout is a value computed from the score and the available width, and that is
  the only reason it can be unit tested at all.
- **Integer ticks at 3840 made the written-value lookup exact.** Every value
  from a breve to a 128th, with one or two dots, and every triplet and
  quintuplet of them, lands on an integer. `NoteValue` compares integers and
  reports anything that does not match, rather than rounding — which is the same
  stance `rescaleToTicks` takes one layer down.
- **A chord being one event with several pitches is what a renderer wants.** One
  stem, several noteheads, one duration. Modelling a chord as simultaneous
  events would have meant grouping them back together here, by onset and voice,
  and guessing.

## Findings

### 1. The model keeps a spelling and cannot render it — *fixed here*

`Pitch` stores `step`, `alter` and `octave` precisely so that F♯ and G♭ stay
distinguishable, and offered nothing that turns them into a name. The only
note-naming function in the repository, `noteNameForMidi`, is MIDI-only — so it
*cannot* make the distinction the model exists to preserve — and lives inside
`src/features/analysis/tuner/`, which a different tool may not reach into.

The first consumer would therefore have written its own, and so would the
second, and they would have disagreed about enharmonics and about how to spell a
double flat.

**Done:** `score::spelledName` in `src/platform/score/PitchNames.h`.

### 2. There is no shared reference pitch, so a target cannot be heard

The target note the tool shows carries a sounding MIDI number. Turning that into
a frequency — which is what comparing it against live audio needs — requires a
reference pitch, and the only one in the codebase is:

```cpp
// src/features/analysis/tuner/PitchTracker.cpp
constexpr double referenceFrequencyHz = 440.0;
```

Private to the tuner. The Target Note tool is handed `SharedPitchAnalysis` like
every other tool and still cannot say whether what it is hearing is the note it
is showing, because duplicating that constant would put two reference pitches in
one application and guarantee they disagree the first time one of them becomes a
setting.

**Should happen:** a reference pitch that belongs to the application rather than
to a tool, settable once. #27–#29 are built on pitch matching and this blocks
all of them; it is the single most consequential thing on this list.

### 3. Walking to a note is a hand-written nested loop

Getting from a `Score` to its first pitched note is: parts → measures → voices →
events, skipping rests, skipping grace notes, and comparing onsets *across*
voices because a measure's voices are independent timelines. That is about forty
lines, and it is forty lines that #32's renderer, #33's cursor, and playback will
each write again.

The model offers `measureCount` and `totalLength` as free functions and nothing
at all for events.

**Should happen:** event iteration in `score::`, even if only
`firstPitchedEvent` and a forward iterator. Each consumer writing its own is how
four slightly different definitions of "the next note" end up in one
application.

### 4. Absolute position is a sum the consumer has to remember

`ScoreEvent::onset` is measure-relative, by a deliberate decision the header
explains well (a measure can be examined and repaired on its own). `Measure::start`
is absolute. So an absolute tick is `measure.start + event.onset`, computed by
the caller, and a caller that forgets is wrong in a way that is correct in bar 1
and silently wrong everywhere else — the worst possible failure shape for
something a test fixture usually exercises in bar 1.

**Should happen:** `absoluteOnset(measure, event)`, or an iteration API (3) that
yields positions already absolute.

### 5. A duration in seconds is not a duration converted

The obvious way to get a note's length in seconds —
`tickToSeconds(event.duration)` — is wrong whenever the tempo changes while the
note is sounding. The correct form is
`tickToSeconds(start + duration) - tickToSeconds(start)`.

`TempoMap` exposes `tickToSeconds` and `secondsToTick` and nothing that takes a
span, so the wrong form is the shorter one and the right form has to be known.

**Should happen:** `TempoMap::spanSeconds(Tick from, Tick length)`.

### 6. "The first note" is not a question the model can answer

Three separate gaps, all of which this change had to decide for itself with
nothing in the model to appeal to:

- **Which part?** `parts.front()` is a convention invented here. Nothing says a
  score has a principal part, and nothing should — but a consumer needs
  *something*, and #33's part selection is where this is settled.
- **Which voice first?** Invariant 2 orders events *within* a voice. Across
  voices there is no stated order, and `Voice::number` is explicitly not an
  index (a part may use voices 1 and 5). This change compares onsets and
  tie-breaks on the lower number; another consumer will reasonably choose
  differently.
- **Which note of a chord?** `ScoreEvent::notes` is documented as "one entry for
  a note, two or more for a chord" and says nothing about their order. Lowest
  first? Source order? A consumer picking "the chord's note" is picking
  arbitrarily and cannot tell that it is.

**Should happen:** state the chord ordering in `ScoreEvent` (it is free — the
importer already produces *some* order, and writing it down makes it a
guarantee), and let #33 settle part and voice selection properly.

## Findings outside the model

### 7. A score cannot be got into a tool under the capture harness — *fixed by #32*

`src/application/testcontrol` can put the application into a state that imports a
fixture, and the only route it has is `startScoreImport`, which also opens the
import summary window. An overlapping top-level window corrupts a capture — the
harness's own comment records this happening — so **no surface about a score
consumer other than the summary window can be photographed.**

The Target Note tool's populated state was the first casualty: it has an approved
state and a surface for its *empty* state only, and the one picture actually
worth taking could not be taken.

**Done, by `score-notation-renderer`.** For a tool that shows a note as text
this was an inconvenience. For a renderer it was fatal — the entire deliverable
is a picture — so it had to be fixed there rather than deferred again.

`ApprovedWindowState::showScoreImportSummary` defaults to true, so the two
states that are *about* the summary are untouched; a state that opens a score
into a tool sets it false, `MainComponent::finishScoreImport` honours it, and
the flag is cleared on the way into every state so it cannot leak between
captures. The invariant the old tests protected — one subject per picture — was
restated rather than dropped: a state showing the summary opens nothing else,
and a state suppressing it opens exactly one tool.

What remains for the Target Note tool is one approved state, which nothing now
prevents.

### 8. Adding a tool costs more than the two entries it is documented to cost

`adding-a-tool.md` and `BuiltInToolCatalog.h` both say a tool is one catalog
entry plus one factory. The shell side of that is true and was a pleasure. The
rest of the repository asks for four more things, three of them enforced by
tests that fail only after the tool exists:

| What | Enforced by |
|---|---|
| An approved window state naming the tool | `every tool is reachable in at least one state` |
| Inclusion in `all-tools-docked` | `every tool can be open at once` |
| A hardcoded tool count, bumped | `the built-in catalog declares every shipped tool` |
| A surface in `tools/scripts/testing_suite/surfaces.py` | nothing — it is simply missing otherwise |

None of these is wrong to require, and the hardcoded count is a deliberate
speed bump: adding to the tool roster should be something a reviewer sees. What
was wrong is that the documentation promised two entries, so each of the four
was learned by a test failing *after* the tool already worked.

**Done:** `adding-a-tool.md` now has a "Make it verifiable" step and all four in
its checklist.

### 9. A fourth docked tool makes #150 unmissable

With `target-note` added to `all-tools-docked`, the split workspace at 1280×800
shows the tuner at full width, the spectrogram squeezed, and the third and
fourth panels clipped off the right edge. That is #150 exactly, one tool worse.
Nothing here caused it and nothing here should fix it, but the reference image
for that state is now a picture of the bug, which is worth knowing before
someone approves it.

**Worse again since.** `score-view` is the fifth tool in that state, and it is
the one that most needs width. The reference image for `all-tools-docked` is now
a picture of the bug with a renderer in it.

### 10. A dynamic marking is read correctly and reported as unread

Found by `score-notation-renderer`, and the only finding here that is a defect
in the importer rather than a shape in the model.

`<direction-type><dynamics><mf/></dynamics></direction-type>` is imported
correctly — `MusicXmlMeasureReader` recognises the marking through
`isDynamicMarking` and produces a `Direction` for it — and the file *also* gets
a diagnostic saying "The importer does not read this element, so it was
ignored." The marking names live in `isDynamicMarking` and not in
`knownElements`, and `countUnrecognisedElements` walks every element in the
document against the latter.

So every real score with dynamics in it carries one misleading diagnostic per
distinct marking, saying the opposite of what happened. This is not cosmetic:
the import summary is how a user decides whether the file was read properly.

It also has a direct cost here. `notation-sample.musicxml` was written with a
dynamic in it and had to have it taken out again: a fixture whose picture is of
the renderer cannot import with a diagnostic, because the surface would then
show "1 import diagnostic" and say the file was not fully read. The renderer's
handling of dynamics is covered by `ScoreLayoutEngineTests` instead, which
builds a `score::Direction` directly.

**Should happen:** add the dynamic marking names to `knownElements`, or skip the
children of a `<dynamics>` element in `countUnrecognisedElements`. Deliberately
*not* fixed by #32: the fix changes the pinned diagnostic count of most of the
50-plus files in `expectations.txt`, and that corpus file's own header says
"'The importer changed' is not a reason to edit this file, it is a reason to look
at the diff." That diff belongs to a change about the importer.

### 11. `<part-group>` is still dropped, and #200 will need it

Recorded on #32 when #31 landed, and still true: which parts brace together on
the page was judged engraving data and is dropped with a diagnostic.

#32 does not need it, because it draws one staff of one part. **#200 does** — a
braced grand staff is exactly the bracketing that is not imported. The comment
on #32 already called capturing it "a small additive change to the importer",
and this is the note that it is now the next change's problem rather than a
hypothetical.

## Where these go

Findings 2, 3, 4, 5 and 6 belong under #13 as issues against the score model.
Finding 8 is a documentation and test issue, 9 is evidence on the existing #150,
10 is an issue against the importer under #31, and 11 is a prerequisite to note
on #200.

Findings 1 and 7 are done: 1 by `score-target-note-tool`, which added
`score::spelledName`, and 7 by `score-notation-renderer`, which could not have
been verified at all without it.
