## 1. Written note values

The foundation: everything below needs to know how many flags and how many dots
an event has, and the model does not say.

- [x] 1.1 Add `NoteValue.h`/`.cpp` to `src/platform/score/engraving/`:
      `noteValueFor(Tick duration, const TupletRatio&)` returning the notehead
      glyph, flag count and dot count, with the tuplet ratio applied before the
      decomposition.
- [x] 1.2 Report a duration that is not a dotted power of two, rather than
      drawing it short in silence. The result carries enough for the caller to
      raise the diagnostic with the measure's printed number.
- [x] 1.3 Replace `noteheadFor` and `restGlyphFor` in `ScoreLayout.cpp` with
      calls into it, so there is one derivation and not three.
- [x] 1.4 Tests in `src/tests/platform/score/engraving/NoteValueTests.cpp`:
      every plain value from whole to 64th; one, two and three dots; a triplet
      eighth and a triplet quarter; and an inexpressible duration producing the
      largest value that fits plus the flag that says so.

## 2. Stems and flags

- [x] 2.1 Add a `PlacedStem` to `LayoutTypes.h`: x, the step it starts at, the
      step it ends at. Positions only, as everything there is.
- [x] 2.2 Decide direction on the `PreparedEvent`, in the preparation pass where
      the voice is still in hand: voice first when the staff has more than one
      in that measure, then the chord's note furthest from the middle line, then
      the single note's position with the middle line down.
- [x] 2.3 Place one stem per event from the outermost notehead, on the right for
      an up stem and the left for a down one, at the font's own stem thickness.
- [x] 2.4 Default length one octave (seven steps), extended so a note outside
      the staff still reaches the middle line.
- [x] 2.5 Place a flag at the stem's far end for an unbeamed event with one or
      more flags, choosing the up or down glyph to match the stem. `flag8thUp`,
      `flag8thDown`, `flag16thUp`, `flag16thDown` are already in the metrics
      table; anything shorter than a sixteenth reuses the sixteenth glyph and
      records a diagnostic.
- [x] 2.6 Place augmentation dots after the notehead, nudged off a staff line
      into the space above when the note sits on a line.
- [x] 2.7 Tests: direction for each of the three rules including the middle-line
      tie; one stem for a chord of three, from the outermost note; a stem on the
      correct side; a flag matching its stem's direction; a dot off the line.

## 3. Beams

- [ ] 3.1 Beat grouping from the prevailing `TimeSignature`: three divisions of
      the beat type when compound (`beats % 3 == 0 && beats > 3`), one when
      simple. A pure function with its own tests.
- [ ] 3.2 Collect beam groups: consecutive beamable events — flags ≥ 1, not a
      rest, one voice, one staff — within one beat group. A group of one keeps
      its flag.
- [ ] 3.3 Add a `PlacedBeam` to `LayoutTypes.h` as four corners plus the beam
      index, so the painter fills a path and computes nothing.
- [ ] 3.4 Compute slope from the group's first and last notehead, clamped, and
      a single direction for the whole group from its outermost note.
- [ ] 3.5 Re-length every stem in a group so its far end lies on the beam at its
      own x.
- [ ] 3.6 Secondary beams for sixteenths and shorter, offset inward, spanning
      only the events that carry them.
- [ ] 3.7 Tests with **named cases**, as the proposal's long-term section
      requires: 6/8 into two threes, 3/4 into three twos, 4/4, cut time, a rest
      breaking a group, a group of one keeping its flag, every stem landing on
      its beam, and a clamped slope over an octave leap.

## 4. Rests and voices

- [ ] 4.1 Dot rests from the same note value derivation.
- [ ] 4.2 Offset rests per voice on a staff carrying more than one, so two
      voices' rests do not coincide.
- [ ] 4.3 Tests for both.

## 5. Spacing

- [x] 5.1 Include stem and flag width in an event's required width, so a flag
      does not overhang the next column.
- [x] 5.2 Confirm the existing column tests still hold, and update the ones
      whose numbers legitimately change rather than loosening them.

## 6. Painting

- [x] 6.1 Draw stems as filled rectangles and beams as filled quadrilaterals in
      `ScoreViewComponent`, from the positions the layout produced.
- [x] 6.2 Confirm the painter computes no slope, length or corner — the
      assertion the whole split rests on.
- [x] 6.3 Add any new sources to `tools/cmake/Sources.cmake`.

## 7. Verification

- [ ] 7.1 `PracticeTakesTests` green.
- [ ] 7.2 `clang-format` via pre-commit and `clang-tidy` locally.
- [ ] 7.3 Extend `engraved-page.musicxml` with a 6/8 passage and a dotted
      rhythm, and update the `score-view-page` surface questions: the
      "rhythm-not-yet-drawn" known gap goes away and becomes questions about
      beam grouping.
- [ ] 7.4 Capture the surface and look at it, rather than declaring it done from
      a green test run.
- [ ] 7.5 Update `docs/development/architecture/ARCHITECTURE.md` with the new
      engraving sources.
