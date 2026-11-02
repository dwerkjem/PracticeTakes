## Why

Practice Takes is written in English and has no way to be anything else. There
is no localisation of any kind in `src/` — no `TRANS`, no catalogue, no locale
— and roughly 500 user-facing sentences sit as literals in the code that draws
them.

The application teaches somebody to read music. Reading is the task, so the
language the instructions are in is not decoration, and the audience for a
practice tool is not disproportionately English-speaking. Classical training in
particular is concentrated in exactly the places this application cannot speak
to.

Three things make this worth proposing now rather than later:

- **Every month of new code makes it bigger.** The string count grows with each
  feature, and each one written as a concatenated sentence is one more to take
  apart later.
- **The sentences are built in a way that cannot be translated**, not merely
  untranslated — `"bar " + number`, `" (seen " << count << " times)"`,
  `"Bar X  -  part, staff Y, voice Z"`. Word order and plural rules are baked
  into `+`. This is not a find-and-replace away from working.
- **Some of it is the wrong shape architecturally anyway.** The MusicXML
  importer composes English prose inside `src/platform/score/`, which is
  JUCE-free by design, and deduplicates diagnostics by comparing that prose.
  Text belongs above the platform layer whether or not it is ever translated.

## What Changes

### The mechanism

- **A localisation layer** under `src/application/localisation/`, built on
  JUCE's `LocalisedStrings` so there is no new dependency, but keyed on
  **symbolic identifiers** (`score.import.slur.unsupported`) rather than on
  English source text. Keying on English means every edit to an English
  sentence silently orphans five translations.
- **English becomes a catalogue too**, not a set of literals. It is the
  fallback every other locale falls back to, and it makes "which strings does
  this application show?" a question with a file for an answer instead of a
  grep.
- **Formatting with named placeholders** — `{bar}`, `{voice}` — so a translator
  can reorder them, and **plural selection** by CLDR category rather than by
  `if (n == 1)`.
- **Two CI checks**: every key used in code exists in the English catalogue, and
  every key in the catalogue is used by something. A missing string becomes a
  failed build rather than a word that shows up as an identifier in front of a
  user.

### The layering

- **Text stops originating below `src/application`.** Importer diagnostics
  become a code plus typed parameters; rendering moves to
  `src/application/shell/ui/score/`. Deduplication keys on the code and its
  parameters rather than on a rendered sentence.

### Musical naming, as its own setting

- **Note naming is not a translation and is not tied to interface language.** A
  German musician reads H where this application says B, and may well want the
  interface in English. A `NoteNaming` preference, separate from the language
  preference, covering English/Dutch, German, and Romance solfège, with octave
  numbering (scientific `C4`, Yamaha `C3`, Helmholtz `c′`) as its own choice
  again.
- **One naming path instead of two.** The tuner has its own sharp-only twelve-name
  table (`PitchTracker.cpp`); the score model has `score::Step` with a written
  spelling. They are unified in the JUCE-free score layer, which also fixes a
  standing bug: the tuner shows G♯ in a piece whose key calls it A♭.

### Scope

**First languages: German, French, Spanish, Italian, Portuguese.** All are
covered by the already-embedded IBM Plex Sans, so this proposal needs no
typography work at all.

### Non-goals

- **CJK and right-to-left.** Both are real and both are out. `AppLookAndFeel`
  returns embedded Plex for *every* font, so CJK text would render as tofu
  today, and fixing that means per-script fallback and several megabytes per
  family. JUCE's right-to-left support would not carry Arabic or Hebrew without
  custom layout work. The design records what each would need so the mechanism
  does not foreclose them.
- **Translating the application into anything, in this change.** What lands
  here is the mechanism, the English catalogue and the naming axis. The first
  real translation is a separate, much smaller change.
- **Localising developer-facing text** — log lines, the `feedback-intake`
  worker's operator messages, and anything only a maintainer reads.
- **Localising the test-control vocabulary.** Approved-state names and their
  descriptions are a contract with the capture harness; translating them breaks
  `list-states`. The design makes this an explicit exclusion rather than a trap
  for whoever does the sweep.

## Capabilities

### Added Capabilities

- `localisation`: how user-facing text is identified, stored, chosen,
  formatted, and verified; what is deliberately never localised; and how
  musical naming is kept independent of interface language.

### Modified Capabilities

- `musicxml-import`: diagnostics become a stable code plus typed parameters
  rather than an English sentence, and are deduplicated on that code. What the
  importer accepts, repairs and refuses is unchanged — this is about how a
  finding is *reported*, not what counts as one.

## Impact

- **New**: `src/application/localisation/` and one catalogue per locale under
  `tools/packaging/` or `docs/` — placement decided in the design against the
  repository-root constraint, not invented here.
- **Changed, widely but shallowly**: roughly 500 literals across
  `shell/ui/settings`, `shell/ui/workspace`, `features/feedback`,
  `features/analysis/*`, `shell/ui/score`. Mechanical, but it touches nearly
  every UI file, so it wants to land before the next large feature rather than
  after.
- **Changed, deeply but narrowly**: `MusicXmlDiagnosticSink` and
  `ScoreImportSummary`, which is where prose and the platform layer currently
  meet.
- **Changed**: `PitchTracker` and the score model's spelling, unified.
- **Verification cost is the real risk.** The capture sweep is already 40
  surfaces across resolutions and palettes, and multiplying that by five
  languages produces a grid no person will review. The design's answer is a
  pseudolocale rather than more languages; see there.
- **Blocked by**: nothing. It conflicts with any large in-flight UI branch,
  though, so sequencing against #225/#238 matters more than dependencies do.

## Long-term compatibility

The lasting commitment is the **key namespace**, not the English wording. Once
translators have worked against `score.import.slur.unsupported`, that
identifier is a stable name: the English text behind it can be reworded freely,
but renaming or silently repurposing the key discards work done in five
languages. Keys are therefore append-and-deprecate, like a wire format, and the
design says what retiring one means.

The second commitment is narrower and worth stating: **note naming is a user's
choice and not a consequence of their language**. Collapsing the two settings
later would be a behaviour change for anybody who set them apart on purpose.
