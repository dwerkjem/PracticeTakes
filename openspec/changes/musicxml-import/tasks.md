## 0. Decisions before any code

- [ ] 0.1 Resolve the six items in `design.md` § "Decisions needed from Derek":
      the owning layer, the musical time representation, as-written versus
      as-played, whether MusicXML and MIDI share a model, the parser and
      dependency policy, and the test-fixture and licensing policy.
- [ ] 0.2 Record each resolution in `design.md` under a "Resolved questions"
      section, following the convention in
      `openspec/changes/archive/2026-07-31-close-highest-risk-test-gaps/design.md`.
- [ ] 0.3 Fix the directory and namespace names implied by decision 1, and
      update the placeholder paths used in the tasks below.

## 1. Model types

- [ ] 1.1 Add the pitch type storing step, alteration, octave, and the derived
      sounding number, with construction from a spelling and the consistency
      check that invariant 5 requires.
- [ ] 1.2 Add the duration and position types for the chosen time
      representation, with the conversions between the source's units and the
      model's, and the rescale factor kept on the score.
- [ ] 1.3 Add the event types — note, chord, rest — carrying staff, onset,
      duration, grace flag, tuplet ratio, tie linkage, and lyric syllables.
- [ ] 1.4 Add the lyric syllable type with verse number, syllabic position,
      text, and extend flag.
- [ ] 1.5 Add the clef, key signature, and time signature types, and the
      direction type covering tempo and dynamic markings.
- [ ] 1.6 Add the measure type: printed number as a string, zero-based index,
      absolute start, nominal duration, pickup flag, attribute changes, repeat
      and ending data held uninterpreted, and voices.
- [ ] 1.7 Add the part and score root types, including the work and credit
      metadata and the encoding-software string.
- [ ] 1.8 Add the tempo map with position-to-seconds and seconds-to-position
      conversion and the documented default tempo.
- [ ] 1.9 Add the diagnostic type: severity, optional location (part, printed
      measure number, voice, position), element name, and message.
- [ ] 1.10 Confirm no model header includes `JuceHeader.h` or any GUI module,
      and that the model compiles against the module set `PracticeTakesTests`
      already links.

## 2. Model construction and invariants

- [ ] 2.1 Add the builder that assembles a score and produces an immutable
      value; make the constructed score shareable as read-only and unmutatable
      afterwards.
- [ ] 2.2 Implement the invariant checks: voice ordering and non-overlap,
      non-negative durations, measure-duration bounds with the pickup
      exemption, tie-chain matching, unique non-empty part identifiers,
      cross-part measure alignment, tempo-map ordering, and diagnostic
      locations referencing only existing entities.
- [ ] 2.3 Make each invariant violation a repair plus a diagnostic, not a
      throw, and record in the code comment which repair applies to which
      violation.
- [ ] 2.4 Add `tests/ScoreModelTests.cpp` covering every invariant, each with a
      case that satisfies it and a case that must be repaired.
- [ ] 2.5 Add `tests/TempoMapTests.cpp` covering single tempo, mid-score tempo
      change, no declared tempo, duplicate positions, and round-tripping
      position to seconds and back.
- [ ] 2.6 Add the new sources to `target_sources(PracticeTakes ...)` and to
      `add_executable(PracticeTakesTests ...)` in `CMakeLists.txt`, keeping the
      lists alphabetically grouped as they already are.

## 3. File acceptance and the parser adapter

- [ ] 3.1 Add the XML adapter header that is the only place the chosen XML
      parser is named, exposing document loading and element traversal.
- [ ] 3.2 Verify the parser performs no network access when a document declares
      an external document type, and bound or strip inline entity expansion;
      record the finding in `design.md`.
- [ ] 3.3 Add source-file, uncompressed-size, and expansion-ratio limits as
      named constants next to the code that enforces them, following the
      `maximumDocumentBytes` convention in `SettingsTransferCodec`.
- [ ] 3.4 Add compressed-container handling: read the container manifest,
      resolve the root score document, and fail with a distinct status when the
      manifest is absent or names an entry that is not present.
- [ ] 3.5 Add the import status enum and result struct following the
      `enum class ...Status` plus `struct ...Result` convention, including the
      partial-success status modelled on `WorkspaceCatalogDecodeStatus`.
- [ ] 3.6 Add `tests/MusicXmlContainerTests.cpp` covering a valid container, a
      container with no manifest, a manifest naming a missing entry, an
      expansion-ratio violation, an oversized source file, a missing file, an
      unreadable file, malformed XML, a well-formed non-MusicXML document, and a
      timewise document.
- [ ] 3.7 Assert in each failure test that the result carries no score.

## 4. Importer: structure and time

- [ ] 4.1 Import the part list, part identifiers, names, abbreviations, and
      staff counts, generating identifiers with a diagnostic when the source's
      are missing or duplicated.
- [ ] 4.2 Import measures with their printed numbers and pickup flags, and
      reconcile differing measure counts across parts with padding and a
      diagnostic.
- [ ] 4.3 Import source duration units per part, including a mid-score change,
      and rescale into the model's time base; diagnose any rescale that is not
      exact.
- [ ] 4.4 Implement the voice cursor, including backward and forward moves, and
      order each voice's events by position.
- [ ] 4.5 Enforce the measure-duration bound per voice with the pickup
      exemption, truncating and diagnosing over-full measures.
- [ ] 4.6 Add `tests/MusicXmlTimingTests.cpp` covering two- and four-voice
      measures, a backward move to a non-zero position, a forward move past the
      end of a measure, a mid-score change of source duration units, a pickup
      measure, an over-full measure, and an under-full non-pickup measure.

## 5. Importer: pitch, chords, ties

- [ ] 5.1 Import pitched notes with their spelling and derive the sounding
      number.
- [ ] 5.2 Import rests, including whole-measure rests.
- [ ] 5.3 Collapse a run of simultaneous notes into a single chord event
      consuming one duration.
- [ ] 5.4 Import ties as sounding links and confirm slurs produce no tie link;
      drop unmatched tie ends with a diagnostic.
- [ ] 5.5 Import grace notes as zero-duration events that do not advance the
      cursor.
- [ ] 5.6 Add `tests/MusicXmlNoteTests.cpp` covering enharmonic spellings, a
      three-note chord, a tie across a barline, a tie with no end, a slur that
      must not become a tie, a whole-measure rest, and a grace note.

## 6. Importer: attributes, directions, lyrics

- [ ] 6.1 Import clef per staff, key signature, and time signature, including
      changes partway through the score.
- [ ] 6.2 Import tempo from both the sounding attribute and the metronome
      marking, and define and test which wins when they disagree.
- [ ] 6.3 Import dynamics as directions attached to a part, measure, and
      position.
- [ ] 6.4 Import lyrics with verse number, syllabic position, text, and extend,
      attaching each syllable to the correct note.
- [ ] 6.5 Add `tests/MusicXmlAttributeTests.cpp` and
      `tests/MusicXmlLyricTests.cpp` covering mid-score clef, key, and time
      changes, conflicting tempo sources, dynamics placement, multiple verses,
      a melisma, and a syllable on a chord.

## 7. Subset boundary and diagnostics

- [ ] 7.1 Drop each recognised-but-unsupported construct listed in `design.md`
      with a diagnostic naming it.
- [ ] 7.2 Summarise unrecognised elements once per element name with an
      occurrence count, not once per occurrence.
- [ ] 7.3 Confirm no unsupported or unrecognised content can turn a successful
      import into a failure.
- [ ] 7.4 Add `tests/MusicXmlDiagnosticsTests.cpp` covering diagnostic
      locations for a per-measure problem, a non-numeric measure number, a
      document-level problem with no location, aggregation of a repeated
      unrecognised element, and an import that succeeds despite dropped
      content.
- [ ] 7.5 Decide and implement the behaviour for a file that imports with zero
      notes (see `design.md` § Open questions), and test it either way.

## 8. Real-score corpus

- [ ] 8.1 Assemble the corpus agreed in decision 6: public-domain repertoire
      exported by us, covering at least one multi-part vocal score and one
      piano score, from more than one notation program if available.
- [ ] 8.2 Record each file's provenance and licence status in a README beside
      the corpus.
- [ ] 8.3 Wire the corpus path into the test target with a compile definition,
      following the `PRACTICE_TAKES_SOURCE_DIR` precedent on the app target.
- [ ] 8.4 Add `tests/MusicXmlCorpusTests.cpp` asserting each file's part count,
      measure count, total musical length, and diagnostic count, so a change in
      conversion fails rather than passing silently.
- [ ] 8.5 Note in the test file which notation programs the corpus came from, so
      a reader knows which dialects are actually covered.

## 9. Documentation and verification

- [ ] 9.1 Write the supported-subset document listing imported constructs,
      recognised-but-dropped constructs, and rejected document types, and link
      it from `docs/development/README.md`.
- [ ] 9.2 Add a score-model section to `docs/development/ARCHITECTURE.md`
      covering ownership, immutability, the background-thread import, and the
      rule that the audio thread never reads the model.
- [ ] 9.3 Add a follow-up issue for transposing instruments, which this change
      deliberately drops.
- [ ] 9.4 Add follow-up notes to #32, #33, #34, and #39 pointing at the model
      and at whichever decisions in this change constrain them.
- [ ] 9.5 Run `python3 scripts/run_tests.py` and the C++ suite
      (`PracticeTakesTests`) and confirm both pass.
- [ ] 9.6 Run `clang-format` and `clang-tidy` via pre-commit and confirm the new
      sources are clean.
- [ ] 9.7 Re-read the change's spec deltas against the implemented behaviour and
      correct any requirement the implementation had to deviate from, recording
      the deviation rather than quietly editing the spec to match.
