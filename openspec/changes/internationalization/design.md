## Context

What the survey found, since the design follows from it rather than from
general i18n practice:

- **No localisation exists.** No `TRANS`, `LocalisedStrings`, `setlocale`, or
  catalogue anywhere in `src/`.
- **~559 sentence-like literals** in non-test `src/`, concentrated in
  `shell/ui/settings` (71), `shell/ui/workspace/shell` (60),
  `features/feedback` (56), `application/testcontrol` (55),
  `application/configuration` (33), `score/musicxml/read` (31),
  `shell/ui/score` (30).
- **55 of those must never be touched.** `application/testcontrol` holds
  approved-state names and descriptions that the capture harness matches on.
- **Sentences are concatenated**, so word order and plurals are baked in:
  `ScoreImportSummary.cpp:135` `"bar " + measureNumber`; `:140`
  `"voice " + std::to_string(voice)`; `:398` `" (seen " << occurrences << " times)"`;
  `TargetNoteComponent.cpp:160` `"Bar " + n + "  -  " + part + ", staff " + s + ", voice " + v`.
- **Prose is produced in a JUCE-free layer.**
  `MusicXmlDiagnosticSink::addUnsupported(location, elementName, message)` takes
  an English `std::string`, and the sink deduplicates on
  `(severity, elementName, message)`.
- **Two pitch-naming paths.** `PitchTracker.cpp:12` has a sharp-only twelve-name
  table; `score::Step` carries MusicXML's written spelling.
- **The UI font is embedded and unconditional.** `AppLookAndFeel::getTypefaceForFont`
  returns IBM Plex Sans for every font, and `AppLookAndFeelTests` asserts it, so a
  fallback fails the suite. Plex Sans covers Latin, Greek and Cyrillic.
- **Verification is already large**: 40 surfaces, each across resolutions and
  two palettes.

## Goals / Non-Goals

**Goals**

- Every user-facing string addressable by a stable identifier, with English as
  data rather than as literals.
- A translator can reorder placeholders and supply correct plurals without
  touching code.
- Musical naming settable independently of interface language.
- The cost of verifying a translated build is bounded and does not scale with
  the number of languages.

**Non-Goals**

- CJK, right-to-left, and the typography both need.
- Shipping any translation in this change.
- Localising developer-facing or harness-facing text.

## Decisions

### 1. JUCE's `LocalisedStrings` for storage, but keyed on identifiers

JUCE 8 ships `LocalisedStrings`, `TRANS()` and `setCurrentMappings`, so the
storage, parsing and lookup are free and add no dependency. What JUCE intends —
`TRANS("Reading the score...")`, keyed on the English — is rejected:

- Editing English rewording silently orphans every translation of that line,
  with no error, because the key no longer matches.
- Two different senses of one English word collapse into one entry. This domain
  has that problem immediately: **"bar"** is a measure in the import report
  (German *Takt*) and a display mode in the tuner (German *Balken*). Keyed on
  English they are one string and one of the two is wrong in every language.
- There is nowhere to put a note for the translator.

So catalogues are keyed on identifiers — `score.import.slur.unsupported`,
`tuner.view.bar` — and the English text is a catalogue entry like any other.

### 2. English is a locale, and that is what makes the checks possible

Because English is a catalogue rather than literals, two invariants become
machine-checkable, and both become CI checks in the style of
`check_test_layout.py`:

- every key referenced in code exists in the English catalogue;
- every key in the English catalogue is referenced by something.

The first prevents a user ever seeing `score.import.slur.unsupported` where a
sentence should be. The second stops the catalogue silting up with strings for
screens that no longer exist, which is what makes translators re-translate dead
text.

Lookup falls back English-ward: chosen locale, then English, then — if a key is
missing from both, which the first check makes a build failure — the key
itself, visibly wrong rather than blank.

### 3. Formatting carries named placeholders and real plural categories

`l10n::format(Key::scoreImportLocation, {{"bar", "12"}, {"voice", "2"}})`
against a catalogue entry of `"bar {bar}, voice {voice}"`. Named rather than
positional because a translator reordering `{0}` and `{1}` is working blind,
and because the compiler cannot help either way.

Plurals select by CLDR category (`one`, `other`, and the room for `few`/`many`
that Polish or Russian would need later) rather than by `n == 1`. The five
target languages need only `one`/`other`, and French puts zero in `one` — small
enough to encode directly, and not worth taking a dependency on ICU, which is
large, awkward to build on three platforms, and aimed at a problem this
application does not have yet. The API shape is what matters: adding `few` later
must not change call sites.

### 4. Text does not originate below `src/application`

`src/platform` is JUCE-free on purpose, and a translation lookup is an
application concern. So the importer stops composing sentences:

```
addUnsupported(location, "slur", "Slurs are phrasing marks; ...")
```

becomes a diagnostic carrying a stable code and typed parameters, rendered in
`ScoreImportSummary`. Three consequences, all improvements:

- The platform layer stays JUCE-free without an exception carved for text.
- Deduplication keys on the code and parameters rather than on rendered prose,
  so what counts as "the same finding" stops depending on how it is worded.
- The importer's findings become testable as data — a test asserts *that a slur
  was unsupported in bar 1*, not that a particular English sentence was
  produced. That is a better test than the one it replaces.

### 5. Musical naming is domain logic, not translated text

"H" is not the German translation of "B". It is a different naming system, and
a user may want either with either interface language — a Dutch player reading
a German edition, an English-speaking student whose teacher says *Do*. Naming
therefore lives in the JUCE-free score layer as `score::PitchNaming`, taking a
convention and a spelling and returning a name. It is not in the catalogue, it
is unit-testable without a display, and it is settable on its own.

Conventions in scope: `englishDutch` (C D E F G A B), `german`
(C D E F G A H, with B meaning B♭), `romance` (Do Re Mi Fa Sol La Si, fixed-do).
Octave numbering is a second, independent choice: `scientific` (middle C is C4),
`yamaha` (C3), `helmholtz` (c′).

Unifying the two existing paths fixes a bug that has nothing to do with
language: the tuner's table is sharp-only, so a piece in F minor has its A♭
announced as G♯. Spelling should follow the score when there is one and the key
when there is not.

### 6. Numbers are formatted, because a tuner shows decimals

`440.0 Hz` is `440,0 Hz` to most of Europe, and a tuner's readout is the most
closely-read number in the application. A `l10n::number()` helper carries the
decimal separator and grouping; `Hz`, `BPM` and cents stay as they are, being
read the same way in all five languages.

### 7. Verification uses a pseudolocale, not more languages

The capture sweep is 40 surfaces across resolutions and palettes. Multiplying
by five languages produces a grid nobody will review, and reviewing it is the
only thing that makes a capture worth taking.

So the sweep stays in English, and a **pseudolocale** is added: generated, not
translated, expanding every string by about 40% and bracketing it —
`[Reading the score… ‚‚‚‚‚‚]`. One extra pass catches what translation actually
breaks in a UI, which is truncation, clipping and unwrapped text, and it catches
it without a translator and without a human deciding whether German prose is
correct. It also catches any string that is *not* in the catalogue, because that
one comes out unbracketed — a find-the-literal tool that costs nothing.

Beyond that, a small named subset is captured in German, chosen because German
is reliably the longest of the five and because its `H` makes it the surface
that proves naming and language are genuinely independent.

The golden-image validation stays English-only: it compares pixels, and
translated text makes that comparison meaningless rather than informative.

### 8. The settings transfer question is left open deliberately

Language and naming are preferences, so they belong in settings. Whether they
travel in an exported `.ptsettings` is a real question with an existing spec
(`settings-transfer`) behind it: the file is meant to be a faithful snapshot,
but importing a colleague's setup and having the interface change language is a
surprise of a different kind from having the theme change.

The recommendation is that they travel and that the import summary says so,
since the summary already reports what it changed — but this is a decision
against an existing capability, so it is raised rather than settled here.

## Implementation notes

Decisions made while building it that the sections above did not settle.

- **Identifiers are string literals, not an enum.** Decision 3's sketch writes
  `Key::scoreImportLocation`; the code writes `l10n::format("score.where", …)`.
  An enum would be a second list of every key to keep in step with the
  catalogue, edited in a header every file includes. The literal is checked
  instead: `check_localisation.py` reads the first argument of every
  qualified `l10n::` call, and a key held in a table is marked where it is
  written with `l10n::key("…")`, a no-op the check can see.
- **Catalogues live in `tools/packaging/localisation/`** (task 1.5), beside the
  fonts, icons and sample score: they are packaging input embedded at build
  time, not source, and the root takes no new entries. Every `*.txt` there is
  embedded, by file name, so adding a language is adding a file. The reason is
  repeated in `en.txt`'s header, where a translator will look.
- **A plural prefers the chosen language's own `other` to English's form.**
  A language that does not distinguish a singular gives only `other`; falling
  through to English's `one` would put an English sentence in the middle of
  it. The order is chosen form, chosen `other`, English form, English `other`.
- **The language code decides plurals and numbers even where its catalogue
  falls back.** An untranslated sentence in a German interface still counts
  `1.200` German-style; a code with no catalogue at all runs wholly in English.
- **The testcontrol exclusion is enforced by the same check** (task 3.2): any
  `l10n::` use or `Localisation.h` include under `src/application/testcontrol`
  fails it, before the capture harness would.

- **An error or reason crossing out of a JUCE-free layer is an identifier.**
  Settings import and export, the workspace catalogue, the take recorder and
  the current target used to return English sentences in `error`/`reason`
  strings. They now return the catalogue identifier (`l10n::key(...)`), which
  the shell renders with `l10n::text`, so those layers stay JUCE-free and the
  tests assert the identifier. The same goes for verdicts with numbers in them
  — the take status, the review rows and summary, the tuner's reading against
  its target — which keep their facts as data in `src/platform` and are put
  into words by `src/application/localisation/TakeText` and
  `features/analysis/tuner/TargetReadingText`.
- **The feedback message stays English.** What a sender submits is read by the
  maintainer, so its scaffolding ("Type:", "Title:", the context tag) is not
  translated, and the preview shows it as it will be sent. The form around it
  is translated. This is the operator-message exclusion, applied to the client.
- **The harness addresses Settings panels by identifier.** `showPanel` used to
  match the tab's printed caption; it now matches a component ID
  (`settings-panel-practice`), so translating "Practice" cannot break an
  approved state. Window titles are still matched by the capture harness and
  are still English in English; the pseudolocale pass (tier 7) is where that
  coupling has to be answered.
- **A workspace action is a table of identifiers, not a verb spliced into
  shared sentences** (`WorkspaceActionText`). The old "Built-in workspaces
  cannot be " + action.toLowerCase() produced "cannot be save workspace."; each
  action now has its own sentence.
- **Built-in workspaces keep their English names in storage** and are shown by
  identifier (`WorkspaceBuiltIns::nameKey`), since the stored name is what a
  user's own workspace may not be called, in a file read by a copy in any
  language.
- **Symbols are not words.** ♯, ♭, ▲, ▼, ¢, −, and the fraction glyphs stay
  literals where they stand alone; where they sit inside a phrase, the phrase
  is the catalogue entry.

- **A diagnostic's code is its catalogue identifier.** `Diagnostic::message`
  became `code` plus `arguments` (named, as strings), and the importer passes
  `l10n::key("score.import.unsupported.slur")` -- so the check sees every code
  the importer can produce, and there is no second table mapping codes to
  entries. A refusal (`MusicXmlImportResult::error`) is the same: an identifier
  with `errorArguments` naming the path, entry or element. The XML parser's own
  message survives as the `detail` of "The document is not valid XML: {detail}".
- **The Score View's notices are the same shape** (`engraving::Notice`), with
  `phraseArguments` for a value that is itself a phrase ("a percussion clef")
  and an optional count for a plural one, rendered by `score_view::noticeText`.
  Task 5 named only the importer's diagnostics; the engraving pass composed the
  same kind of sentence one layer over, so it was taken apart with them.
- **Numbers in the report are grouped** as the language groups them: a
  construct "seen 2,114 times", where it used to say 2114.

## Risks

- **A mechanical change across nearly every UI file.** It conflicts with
  anything large and in-flight, so it should land in a quiet window and not
  alongside #225/#238. This is a scheduling risk, not a technical one.
- **The sweep catches the wrong strings.** `application/testcontrol`'s 55
  descriptions and the approved-state names are a harness contract; translating
  them breaks `list-states` and every capture with it. The exclusion is written
  into the spec, and the pseudolocale pass would reveal the mistake, but the
  surest guard is that the harness's own tests already assert those names.
- **Key churn before the first translation.** Keys are cheap to rename while no
  translation exists and expensive afterwards. The first real translation should
  therefore follow the pseudolocale pass, not precede it.
- **Scope creep into typography.** The moment CJK is attempted,
  `getTypefaceForFont` and its test have to change, and the installer grows by
  megabytes. Keeping the first five languages inside Plex Sans's existing
  coverage is what keeps this change about strings.
