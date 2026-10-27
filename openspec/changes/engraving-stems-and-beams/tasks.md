Settled inputs, from `design.md` § Decisions:

| | |
|---|---|
| Grouping input | Onsets, durations and the time signature. Never the file's `<beam>` elements |
| Group size | One beat, or one dotted beat where `beatType >= 8 && beats % 3 == 0` |
| Breaks | Anything unbeamable, a group boundary, a gap in time, a barline, a voice change |
| Stem direction | One per group, by the note furthest from the middle line; a tie goes down |
| Geometry | A straight line, rise clamped, whole line shifted until every stem clears |
| Stem tip | `StemGlyph::tipSpaces`, a double, replacing `tipPosition` |
| Partial beams | A stub towards the beat, never more than halfway to its neighbour |

## 1. Grouping

- [x] 1.1 Add `src/features/score/engraving/BeamGrouping.h`/`.cpp`, JUCE-free,
      over a `BeamCandidate` holding an onset, a duration and a beam-level
      count. No layout type in the signature: grouping is a question about
      time, and keeping it that way is what makes every metre one assertion.
- [x] 1.2 `beamGroupTicks` and `isCompoundMetre`, with a zero group for a metre
      that cannot be read.
- [x] 1.3 `beamGroupsFor`, returning runs of at least two indices.
- [x] 1.4 Add `src/tests/features/score/engraving/BeamGroupingTests.cpp`: 4/4,
      3/4, 2/2, 6/4, 3/8, 6/8, 9/8, 12/8, 6/16 and an unreadable metre, each
      named; the same six eighths grouped differently by 6/8 and 3/4; four
      sixteenths; a triplet; breaks on a rest, a quarter, a time gap and a
      backwards onset; a lone eighth; and mixed values in one beat.

## 2. Geometry

- [x] 2.1 Add `BeamGeometry.h`/`.cpp`, JUCE-free, over a `BeamStem` holding an
      x, the attach and far notehead positions, and a level count.
- [x] 2.2 The line: natural tips, clamped rise, one shift that satisfies every
      stem's minimum length and far-notehead clearance at once.
- [x] 2.3 Stacked levels, with each stem reaching the outermost that meets it.
- [x] 2.4 Partial beams, pointing back towards the beat and clamped to half the
      distance to the neighbour.
- [x] 2.5 Add `BeamGeometryTests.cpp`: fewer than two stems, a flat beam, a
      slope, the clamp, a note poking through, a chord's far notehead, a
      down-stemmed group, stacked levels both ways, the three partial-beam
      cases, a run of sixteenths among eighths, and a shape built to fight the
      clamp asserted both ways up.

## 3. The layout types

- [x] 3.1 Add `BeamGlyph` and `System::beams` to `ScoreLayout.h`.
- [x] 3.2 Add `StemGlyph::beamLevels`, and replace `tipPosition` with
      `tipSpaces`. One number, one type.
- [x] 3.3 Add the beam options to `LayoutOptions`, each documented as the one
      number a reader would look for: `flagWidth`, `minimumStemLength`,
      `maximumBeamRise`, `beamThickness`, `beamSeparation`,
      `partialBeamLength`.

## 4. The engine

- [x] 4.1 Resolve groups per voice, before stem directions, because a group's
      direction is the group's.
- [x] 4.2 Override stem direction within a group in the single-voice case. The
      multi-voice rule already gives each group one direction, and a group
      never spans two voices.
- [x] 4.3 Emit beams after the measure's stems are placed, since a group needs
      every one of its stems before any of them can be given a tip.
- [x] 4.4 A beamed stem draws no flags.
- [x] 4.5 Derive the slot floor from the notehead and flag widths.
- [x] 4.6 Replace the "not beamed" notice with the grouping-came-from-the-metre
      one, emitted only when a beam was actually drawn. Add the rest and
      grace-note notices, each emitted only when the case occurs.
- [x] 4.7 Extend `ScoreLayoutEngineTests` over the laid-out score: beamed
      eighths lose their flags, a lone eighth keeps one, 6/8 against 3/4
      against cut time, no beam across a barline, a partial beam, one direction
      per group, two voices beamed separately, dots, and the slot floor.
- [x] 4.8 Extend `ScoreLayoutCorpusTests` with what must hold of every beam in
      every real file: finite, forward-running, level in range, the clamp
      respected, no stem carrying both flags and beams, every beamed stem's
      every level covered by a beam at its x, and no beamed stem shorter than
      the minimum.

## 5. The painter

- [x] 5.1 Draw a beam as a filled parallelogram with vertical ends, so the
      stems meet it flush.
- [x] 5.2 Extend each end half a stem's width **along the beam**, not straight
      out: extending x alone slides a sloped bar off the line it was laid out
      on. Found by looking at a capture, not by a test — the magnitude is a
      pixel and no assertion would have been written for it.
- [x] 5.3 Draw beams after stems and before noteheads.

## 6. The harness

- [x] 6.1 Update the `score-view-notation` surface's questions: two about beams
      and one about telling an eighth from a quarter, and the notice question
      now asks for the sentence that replaced the old one.
- [x] 6.2 Update the approved state's description and the fixture's own comment
      to describe what the fixture now draws.
- [x] 6.3 Pin the fixture's beams in `ApprovedScoreFixturesTests`: three beams,
      one of them a second level, four beamed stems and three flagged ones.

## 7. Verification

- [x] 7.1 `ctest --test-dir build --output-on-failure` — 714 tests green,
      including every pre-existing suite.
- [x] 7.2 `run_clang_format.py` (18.1.8, the pinned version) and
      `run_clang_tidy.py` clean over every new and changed file.
- [x] 7.3 `check_test_layout.py` — the new tests mirror `src/`.
- [x] 7.4 `python tools/scripts/run_tests.py` — 737 green, for the
      `surfaces.py` change.
- [x] 7.5 Captured `score-view-notation` headlessly and looked at it, which is
      what found 5.2. Bar 1's eighths and bar 3's sixteenths beam, the second
      beam stacks clear of the first, and every bar meets its stems flush.
- [ ] 7.6 **Not done.** No reference image shows a *partial* beam: nothing in
      the fixture is a lone sixteenth among eighths, and widening the fixture
      would move every count in `ApprovedScoreFixturesTests`. `drawBeam`
      branches on nothing a stub changes — a partial beam is the same filled
      quad between two x values — so the picture of the full beams is a picture
      of that code path, and where a stub is placed is pinned in
      `BeamGeometryTests`. Someone adding a dotted-eighth figure to the fixture
      should pin it then.

## 8. Documentation

- [x] 8.1 Extend `docs/development/architecture/notation-engraving.md`: the two
      new files in the rule table, a Beaming section with the grouping table
      and the geometry rule, the group's stem direction, and the gaps that
      remain moved into *What is not drawn*.
- [x] 8.2 Answer `design.md`'s open questions after implementation, as the two
      previous score changes did.
