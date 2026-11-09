## 1. The mechanism, with nothing translated

Shippable on its own. Nothing visibly changes; every string becomes
addressable.

- [x] 1.1 `src/application/localisation/`: `Catalogue` (load, lookup, fall back
      English-ward), built on `juce::LocalisedStrings` and keyed on identifiers
      rather than English source.
- [x] 1.2 `l10n::text(key)` and `l10n::format(key, named arguments)` with
      `{name}` placeholders a translator can reorder.
- [x] 1.3 `l10n::plural(key, count, ...)` selecting by CLDR category. Only
      `one`/`other` are populated, with French's zero-is-one rule; the signature
      must not change when `few`/`many` are added.
- [x] 1.4 `l10n::number()` for decimal separator and grouping, since `440.0 Hz`
      is `440,0 Hz` in four of the five target languages.
- [x] 1.5 Decide where catalogues live, against the repository-root constraint,
      and say why in the file that holds them.
- [x] 1.6 Tests: fallback order, a missing key, placeholder reordering, plural
      selection per language, and that an unknown locale degrades to English
      rather than failing to start.

## 2. The checks that keep it honest

Before the sweep, so the sweep is verified as it happens rather than after.

- [x] 2.1 A check that every key referenced in code exists in the English
      catalogue. Failing the build is the point: the alternative is a user
      reading `score.import.slur.unsupported`.
- [x] 2.2 A check that every catalogue key is referenced by something, so dead
      strings are not re-translated five times.
- [x] 2.3 Wire both into the Python quality gate beside
      `check_test_layout.py`, and into CI.
- [x] 2.4 Tests for the checkers themselves, including the case that matters:
      a key used only inside a comment or a test must not count as used.

## 3. The exclusions, written down and enforced

- [x] 3.1 Record what is never localised: approved-state names and descriptions
      in `application/testcontrol`, log and developer-facing text, the
      `feedback-intake` worker's operator messages, MusicXML element names
      (`<slur>` is a tag, not a word), and settings keys.
- [x] 3.2 Make the testcontrol exclusion enforceable rather than remembered —
      the capture harness matches on those strings, so translating one breaks
      `list-states` and every capture with it.

## 4. The sweep

Mechanical, wide, shallow. Worth doing in reviewable slices rather than one
commit.

- [ ] 4.1 `shell/ui/settings` (71), the largest and the most self-contained.
- [ ] 4.2 `shell/ui/workspace` (60 + 28 + 11).
- [ ] 4.3 `features/feedback` (56).
- [ ] 4.4 `features/analysis/*` (tuner 20, harmonics 14) — including the
      tuner's "bar" display mode, which must not share a key with a measure.
- [ ] 4.5 `shell/ui/main_window`, `application/tools`, `platform/midi`,
      `features/score`.
- [ ] 4.6 Take apart every concatenated sentence as it is swept. The known ones:
      `ScoreImportSummary.cpp:135,140,398` and `TargetNoteComponent.cpp:160`.

## 5. Diagnostics stop being prose

The one deep change, and an improvement independent of language.

- [ ] 5.1 Replace `addUnsupported/addRepair/addInfo`'s `std::string message`
      with a stable code plus typed parameters.
- [ ] 5.2 Deduplicate on the code and parameters rather than on rendered text,
      so "the same finding" stops depending on wording.
- [ ] 5.3 Render in `src/application/shell/ui/score/`, keeping
      `src/platform/score/` JUCE-free with no exception carved for text.
- [ ] 5.4 Rewrite the importer's tests to assert findings as data — *a slur was
      unsupported in bar 1* — rather than asserting an English sentence. This is
      a better test than the one it replaces, which is the argument for doing it
      here rather than later.
- [ ] 5.5 Confirm `score-import-diagnostics` and `score-import-clean` still
      capture correctly, since the report is rebuilt.

## 6. Musical naming, independent of language

Can run in parallel with sections 1-5; it shares no code with them.

- [ ] 6.1 `score::PitchNaming` in the JUCE-free score layer: convention and
      octave numbering in, name out. Not in the catalogue — "H" is a naming
      system, not a translation.
- [ ] 6.2 Conventions: `englishDutch`, `german` (H, with B meaning B♭),
      `romance` (fixed-do). Octave numbering: `scientific`, `yamaha`,
      `helmholtz`.
- [ ] 6.3 Retire `PitchTracker.cpp`'s sharp-only table and route the tuner
      through it, fixing the standing bug where a piece in F minor has its A♭
      announced as G♯.
- [ ] 6.4 Route the score model's written spelling through the same function, so
      there is one naming path and not two.
- [ ] 6.5 Settings: naming and octave numbering as their own preferences,
      separate from interface language.
- [ ] 6.6 Tests, by name rather than by number: B/H across the boundary,
      B♭ spelled as B in German, middle C in all three octave conventions, and
      a flat-key piece spelled with flats.

## 7. The pseudolocale

- [ ] 7.1 A generated pseudolocale: every string expanded about 40% and
      bracketed. Generated, never hand-written.
- [ ] 7.2 A capture pass in it. The sweep itself stays English — five languages
      across 40 surfaces is a grid nobody reviews, and an unreviewed capture is
      worth nothing.
- [ ] 7.3 Fix what it finds, which will be truncation, clipping and unwrapped
      text. Expect the settings pages and the import report to be worst.
- [ ] 7.4 Note that any string appearing *unbracketed* is one the sweep missed —
      a literal-finder that costs nothing.
- [ ] 7.5 Leave the golden-image validation in English: it compares pixels, and
      translated text makes that comparison meaningless rather than informative.

## 8. Raise the settings-transfer question

- [ ] 8.1 Decide, against the existing `settings-transfer` capability, whether
      language and naming travel in an exported `.ptsettings`. Recommendation:
      they do, and the import summary says so. This is a decision against an
      existing spec, so it is asked rather than assumed.

## 9. Not now, but do not foreclose

- [ ] 9.1 Record what CJK would need: `AppLookAndFeel::getTypefaceForFont`
      returns embedded Plex for every font and `AppLookAndFeelTests` asserts it,
      so per-script fallback means changing both, plus several megabytes per
      family in the installer.
- [ ] 9.2 Record what right-to-left would need, which is more than font
      coverage: mirrored layout, and JUCE support that would not carry it
      without custom work.
