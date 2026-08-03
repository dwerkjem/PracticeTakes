## Decisions needed from Derek

> **All six were resolved on 2026-07-31.** See
> [Resolved Questions](#resolved-questions) below for what was chosen and why.
> The option analysis in this section is kept as the record of what was weighed;
> where a resolution contradicts the recommendation written here, the resolution
> wins.

These six are structural. Each one is cheap to decide now and expensive to
unwind after #32, #33, #34, and #39 are written against it. I have written a
recommendation for each and deliberately did **not** commit to any of them: the
spec deltas in `specs/` are phrased at the behavioural level so they stay valid
whichever way these go. Everything below this section assumes the recommended
option unless it says otherwise; if you pick differently, `tasks.md` sections 1
and 2 change and the rest mostly survives.

### Decision 1 — Which layer owns the score model and the importer

**Question.** Does this live in `src/services/` or `src/features/`?

**Options.**

1. `src/services/score/` for the model plus `src/services/score/musicxml/` for
   the importer. Treats a score as shared infrastructure, like
   `src/services/audio/`.
2. `src/features/notation/` for everything, mirroring
   `src/features/analysis/tuner/`. Treats notation as a user-facing feature
   area, which is what the renderer and score tool will be.
3. Split: model in `src/services/score/`, importer in
   `src/features/notation/import/`.

**Recommendation: option 1.** `ARCHITECTURE_QA.md` states that "a
`src/features/*` tool does not reach into another tool's internals" and that
"shared behavior belongs in `src/services` or `src/application`, not in one
feature depending directly on another". A score is read by the renderer (#32),
the score tool (#33), playback (#35–#38), and sessions (#39) — four consumers,
at least two of which will be separate feature directories. Putting it in
`features/` guarantees a feature-to-feature dependency the checklist forbids.
The renderer and score tool then live in `src/features/notation/` and depend
downward on the service, which is the shape the architecture doc already
describes.

Against option 1: `src/services/` currently holds exactly one thing, and it is
infrastructure in a narrow sense (owning a hardware callback). A score model is
data, not a running service, so "services" is a slightly loose fit. Option 3 is
defensible if you think of the importer as file-format plumbing that only the
notation feature cares about, but it splits one cohesive unit across two layers
for no test or dependency benefit.

**Cost of changing later.** Mechanical but wide: directory move, every relative
`#include` in dependent files, and the explicit file lists in two CMake targets.
Trivial with one consumer, annoying with four. Decide before #32 starts.

### Decision 2 — How musical time is represented

**Question.** MusicXML expresses duration in `<divisions>` units per quarter
note, declared per part and changeable mid-score. The model needs one
representation.

**Options.**

1. **Fixed score-wide PPQ integer ticks.** Pick a constant (3840 is
   `2^8 x 15`, so it divides exactly by 2, 3, 5, and every power of two up to
   256 — clean for 32nd notes, triplets, and quintuplets) and rescale every
   source duration into it. Simple integer arithmetic everywhere downstream;
   inexact conversions round and emit a diagnostic.
2. **Exact rational duration** (numerator/denominator of a whole note),
   normalized on construction. Never loses a duration, at the cost of rational
   arithmetic in every consumer and comparison.
3. **Computed LCM tick base.** Scan the file's divisions values, take the least
   common multiple (capped), and use that as the score's tick base. Exact for
   this file, but the tick base varies per score, so no cross-score constant
   exists and tests must compute expected values rather than state them.

**Recommendation: option 1, with the rescale factor stored on the score so the
original divisions can be reported in diagnostics.** Every downstream consumer
— renderer spacing, cursor position, loop points, MIDI alignment — wants cheap
integer comparison and arithmetic. Real-world divisions values are dominated by
small, highly composite numbers, so exact rescaling into 3840 will be the
overwhelmingly common case, and the rare inexact one is a diagnostic rather
than silent corruption.

Against it: a file using an awkward divisions value (a 7-tuplet at a fine
subdivision) loses exactness, and rounding error accumulates within a measure
unless the last event in a voice is snapped to the measure boundary. Option 2
makes that class of bug impossible and is the "correct" answer if you expect
contemporary or heavily tuplet-ed repertoire; it is more work in every consumer
forever.

**Cost of changing later.** High. This type appears in every entity, every test
assertion, and every consumer's arithmetic. This is the single most expensive
item on this list to revisit.

### Decision 3 — As-written or as-played: repeats and jumps

**Question.** A score with a repeat barline, first/second endings, or a D.C. al
Fine is played in a different measure order than it is written. Does the model
store the written order, the played order, or both?

**Options.**

1. **As written only.** Measures appear once, in source order. Repeat marks,
   endings, and jump directions are stored as data on the measure but are not
   interpreted. Playback order becomes a derived list computed later, in the
   change that needs it.
2. **As played only.** The importer expands repeats so the measure list is the
   performance order. Playback and the cursor become trivial; the renderer must
   undo the expansion to draw the page, and a measure now has several model
   instances, breaking "measure number identifies a measure".
3. **Both**: as-written measures plus an expansion table
   (`playbackOrder: vector<MeasureIndex>`) produced by the importer.

**Recommendation: option 1 now, with the repeat/ending/jump data captured
faithfully so option 3 is a pure addition later.** Nothing in this change plays
anything. The renderer (#32) draws the written score. Expanding repeats
correctly is its own nest of rules (D.S. al Coda, multiple endings, repeats
inside endings) and belongs in the change that first needs to hear it, with its
own tests. Storing the data but not interpreting it costs almost nothing now
and blocks nothing later.

Against it: whoever builds playback inherits the expansion problem at a moment
when they are also building a transport, and a score that repeats will initially
play straight through, which may look like a bug to you when you first hear it.

**Cost of changing later.** Low for option 1 → option 3 (additive). High for
option 1 → option 2 (every consumer's notion of "measure" changes). So the real
question is only whether to reject option 2 permanently, which I recommend.

### Decision 4 — One model for MusicXML and MIDI, or two

**Question.** Issue #34 also promises a "normalized timeline model". Is that
the same type as this one?

**Options.**

1. **Two models, one shared time base.** MusicXML produces a notation-oriented
   `Score` (parts, measures, voices, spelled pitch, lyrics). MIDI produces a
   `Timeline` (tracks, channels, note events, controllers). Both use the same
   tempo-map and tick/seconds conversion, extracted into a shared header.
2. **One model.** MIDI import synthesises parts/measures/voices and MusicXML
   fills in the notation-only fields.
3. **One consumer-facing interface** over two implementations.

**Recommendation: option 1.** MIDI has no spelled pitch (a C-sharp and a D-flat
are the same byte), no voices, no lyrics, and no measures except as inferred
from a time-signature meta event; MusicXML has no channels, no controllers, and
no program changes. Option 2 means every MIDI import fabricates notation
structure that is a guess, and every renderer read of a MIDI-derived score is
reading that guess. Option 3 adds an abstraction whose only two implementations
disagree about what most of it means.

Against it: #32's renderer will only draw `Score`, so "display a MIDI file"
(#13's exit criteria: "a standard MIDI file can be displayed and played")
requires either a MIDI-to-`Score` conversion step — which is option 2's guess,
just made explicit and testable at a single point — or a second, simpler
piano-roll view. That is a real cost and it lands in #34, not here.

**Cost of changing later.** Moderate and one-directional: two models can be
merged behind a shared interface later; one merged model cannot be cleanly split
after consumers depend on its synthesised fields. The shared time base should be
extracted in this change regardless of which way you go.

### Decision 5 — Parser and dependency policy, and what "validation" means

**Question.** Issue #31 asks for validation and for reporting "file location and
reason". How is the XML actually parsed?

**Options.**

1. **JUCE only** — `juce::parseXML` for XML, `juce::ZipFile` for `.mxl`. No new
   dependency; both are in `juce_core`, already linked.
2. **A dedicated XML parser** (pugixml or tinyxml2) via the existing pinned
   `FetchContent` pattern. pugixml reports byte offsets for parse errors and is
   substantially faster on large documents.
3. **A MusicXML library** (libmusicxml, or MuseScore's importer). Licence is the
   blocker: this repository is BSD 3-Clause and MuseScore's importer is GPL.

**Recommendation: option 1, and "validation" means our own structural
validation, not schema validation.** The repository's dependency posture is
minimal and deliberate: JUCE and Catch2 via `FetchContent` pinned to exact
commit SHAs, and vcpkg only for OS libraries. A DOM parse of a whole score fits
comfortably in memory. Option 3 is ruled out by licence unless you find a
permissive one.

Two consequences you should agree to explicitly, because they narrow what #31's
text promised:

- **No XSD or DTD validation.** MusicXML ships DTDs and an XSD, but validating
  against them needs a validating parser (libxml2 or similar — a new dependency)
  and, worse, the DTD is referenced by an external URL. Fetching it at import
  time would be a network call on a file-open path and an XXE-shaped security
  hole. So "invalid content" must mean "violates the structural rules we
  document", not "fails schema validation". This is stricter than a schema in
  the places we care about and looser everywhere else, which I think is the
  right trade for a practice application.
- **Diagnostic location is musical, not textual.** `juce::XmlDocument` exposes a
  parse error only as a string via `getLastParseError()`; there is no line or
  column, and no per-element source position. So a diagnostic says "part P1,
  measure 12, voice 1, event 3" rather than "line 4127". I think that is more
  useful to a musician anyway, but it is not literally what #31 says. Option 2
  (pugixml) would let us also report a byte offset, which is the strongest
  single argument for taking a dependency here.

**Cost of changing later.** Low if the parser is confined behind one adapter
header, which the task list requires. Swapping `juce::parseXML` for pugixml
would then be one file plus the CMake declaration. The validation-scope
decision is harder to revisit because it defines what the test suite asserts.

### Decision 6 — Test fixtures for real scores, and their licensing

**Question.** #31's first acceptance criterion is that "representative vocal and
piano scores import consistently". That needs real files. `tests/` was flat when
this was written and has since been reorganised to mirror `src/`, but it still
has no resource directory, and every existing test that needs a file creates it
at runtime in the temp directory.

**Options.**

1. **Synthetic only** — hand-written MusicXML as raw string literals in the test
   file, matching the existing convention exactly. No licensing question, no new
   directory, but a hand-written fixture only contains bugs we already thought
   of, which is precisely what real exporter output does not do.
2. **A committed corpus** — new `tests/resources/musicxml/` with a
   `PRACTICE_TAKES_TEST_RESOURCES_DIR` compile definition on the test target
   (the app target already does this for `PRACTICE_TAKES_SOURCE_DIR`). Needs
   files that are legally redistributable under BSD-3-Clause distribution:
   public-domain repertoire exported by us from MuseScore, or files from a
   corpus with a clear licence. Adds binary-ish files to the repo and to every
   clone.
3. **Corpus fetched at configure time** via `FetchContent`, skipped when
   offline. Keeps the repo small; makes tests network-dependent, which no
   current test is.

**Recommendation: option 1 for the invariant and boundary tests, plus option 2
for a small corpus — four to six files — of public-domain repertoire that we
export ourselves from at least two different notation programs.** The synthetic
fixtures are what fail informatively when a rule breaks; the real files are what
catch the assumptions we did not know we made. Exporting them ourselves from
public-domain scores (a Bach chorale for vocal SATB, a Clementi sonatina for
piano) sidesteps the licensing question entirely: the music is public domain and
the file is ours.

**What I need from you.** Confirm that committing a handful of exported
public-domain scores under `tests/resources/` is acceptable, and confirm you are
willing to produce exports from a second program (Finale/Sibelius/Dorico output
differs from MuseScore's in ways that matter). If you only have MuseScore, the
corpus tests will over-fit to MuseScore's dialect and we should say so in the
test file.

**Cost of changing later.** Low. Fixtures can be added or swapped at any time.
It is on this list because it introduces a repo convention and a licensing
policy, not because it is hard to reverse.

## Resolved Questions

Decided by Derek on 2026-07-31. Five of six went with the recommendation;
decision 5 did not.

- **Decision 1 — which layer owns the score model and the importer?**
  `src/services/score/` for the model, `src/services/score/musicxml/` for the
  importer, as recommended. A score has four consumers (#32, #33, #35–#38, #39)
  across at least two feature directories, and `ARCHITECTURE_QA.md` forbids one
  feature reaching into another. The renderer and score tool land in
  `src/features/notation/` and depend downward on the service. Namespace is
  lowercase `score`, matching the `namespace performance` precedent in
  `src/features/performance/`.

- **Decision 2 — how is musical time represented?** Fixed score-wide **3840 PPQ
  integer ticks**, as recommended, with the source-to-tick rescale factor stored
  on the `Score` so diagnostics can report the original `<divisions>`. 3840 is
  `2^8 × 15`, so it divides exactly by 2, 3, 5, and every power of two up to 256.
  Inexact rescales round and emit a diagnostic rather than corrupting silently.
  This was flagged as the most expensive item to revisit; it is now fixed.

- **Decision 3 — as-written or as-played?** **As-written only**, as recommended.
  Measures appear once in source order. Repeat barlines, endings, and D.C./D.S.
  jumps are captured faithfully as uninterpreted data on the measure but are not
  expanded. Option 2 (as-played only) is rejected permanently, because it breaks
  "measure number identifies a measure" for every consumer. Adding an expansion
  table (`playbackOrder`) later is a pure addition and belongs to whichever
  change first needs to hear a repeat.

- **Decision 4 — one model for MusicXML and MIDI, or two?** **Two models with one
  shared time base**, as recommended. MusicXML produces the notation-oriented
  `Score` described below; #34's MIDI import produces its own `Timeline`. The
  `TempoMap` and the tick/seconds conversion are extracted into a shared header
  in this change so #34 inherits them rather than reinventing them. Consequence
  accepted: "display a MIDI file" (#13's exit criterion) needs either an explicit
  MIDI-to-`Score` conversion step or a separate piano-roll view, and that cost
  lands in #34, not here.

- **Decision 5 — parser and dependency policy.** Resolved on 2026-07-31 as
  **libmusicxml (Grame) via pinned `FetchContent`**, overturning the JUCE-only
  recommendation on the grounds that the licence objection did not apply to it
  (MPL-2.0, not GPL). **Reversed on 2026-08-02, after verification, back to
  `juce::parseXML`.** The reasoning for the original reversal was sound; the
  library simply does not do what it was chosen to do. What was weighed and what
  was found is in [Verification of libmusicxml](#verification-of-libmusicxml)
  below, and the reversal is deliberately recorded rather than tidied away,
  because the *reason* it failed is a constraint on any future replacement.

  What holds either way:

  1. **The parser does not replace the normalization work.** Rescaling
     `<divisions>` into 3840 PPQ, resolving the `<backup>`/`<forward>` voice
     cursor, collapsing chords, matching ties against slurs, and attaching
     lyrics — `tasks.md` sections 4 through 7 — are the bulk of this change
     whatever parses the XML.
  2. **`.mxl` is ours to handle.** `juce::ZipFile` reads the container and
     resolves `META-INF/container.xml` to its root document. This was true under
     libmusicxml too: its core ships no ZIP reader.
  3. **Validation means our structural rules, not schema validation.** No XSD or
     DTD validation, and no network access on the file-open path. "Invalid"
     means "violates the rules we document".
  4. **Diagnostic location is musical, not textual.** As originally reasoned:
     `juce::XmlDocument` exposes a parse error as a string with no line or
     column, so a diagnostic says "part P1, measure 12, voice 1" rather than
     "line 4127". That is more useful to a musician anyway. The provisional
     hope that libmusicxml might supply a line number came to nothing — it does
     expose one, but the counter is broken (finding 5 below), and it is moot now.

  The adapter-header rule stands and did its job: when the parser was swapped,
  `XmlDocumentAdapter.cpp` was the only file that changed. Nothing in the
  importer, the container reader, or the model ever named a parser.

### Verification of libmusicxml

Checked on 2026-08-02 against upstream commit
`525399683cd9165c483bc20e2459b4149efcd809`, still the tip of the default branch.
It builds, and its headers compile clean under C++20 apart from
`-Wunused-parameter`. Six findings followed. Three were worked around; the
fourth is disqualifying.

1. **No network access, confirmed by source and by execution.** There is no
   socket, HTTP, or curl code anywhere in its `src/` — the only `http://`
   strings are MPL licence header URLs. A document carrying the standard
   MusicXML 4.0 external DTD reference parsed with no fetch.

2. **Entity expansion is impossible, not merely bounded.** Its grammar has no
   `ENTITY` production, so a billion-laughs prologue fails to parse rather than
   expanding.

3. **[Defect, worked around] Not reentrant.** `readbuffer` drives a flex/bison
   parser through file-scope globals, so two imports on two background threads
   would interleave into each other's element stacks. A mutex in the adapter
   fixed it.

4. **[Defect, worked around] Parse-error line numbers drift.** `%option
   yylineno` keeps the counter in a global that is never reset between parses.
   Three consecutive parses of the same malformed document reported lines 2, 3,
   and 4 — the first correct and every one after it wrong, which is worse than
   having no line at all, because it is wrong in a way that looks right.
   `libmxmlset_lineno(1)` before each parse fixed it.

5. **[Defect, worked around] `<!DOCTYPE>` is mandatory.** Its grammar is
   `prolog : xmldecl doctype`, with the alternative that would permit a prolog
   without one present in the grammar file and commented out. A valid MusicXML
   document that omits its DOCTYPE — permitted by the specification, and common
   — was rejected as malformed. Synthesising one in the adapter fixed it.

6. **[Disqualifying] It parses only MusicXML element names.**
   `xmlreader::newElement` asks a fixed factory for every element and returns
   false when the factory does not know it (`factory.cpp:59`,
   `xmlreader.cpp:84`), which aborts the whole parse and writes to `std::cerr`
   on the way out. Three consequences, any one of which is serious:

   - It cannot parse `META-INF/container.xml`. `<container>` and `<rootfile>`
     are not MusicXML elements, so `.mxl` support needed a second XML parser
     regardless — which removes the reason to have this one.
   - It cannot distinguish well-formed non-MusicXML from malformed XML. Both are
     "parse failed", so `notMusicXml` and `malformedXml` could not be told apart.
   - **It defeats task 7.2 outright.** Summarising unrecognised elements is
     impossible when meeting an unrecognised element is fatal. Any vendor
     extension, or any element from a MusicXML version newer than the library,
     fails the entire import. That is the opposite of the robustness #31 asks
     for, and it cannot be worked around without abandoning the library's DOM
     entirely — at which point it supplies nothing but an XML tokeniser, which
     `juce_core` already has.

**What this cost and what it bought.** The change no longer takes a ~120 MB
dependency, no longer introduces weak-copyleft code into a BSD-3-Clause tree,
and no longer needs the third-party notice file and packaging changes that
MPL-2.0 §3.2 would have required. Against that, `juce::parseXML` expands inline
entities and would follow a `SYSTEM` identifier given an input source, so the
adapter **removes the DOCTYPE declaration entirely before parsing**. That makes
both an expansion bomb and an XXE impossible rather than bounded, and costs
nothing: the five predefined entities and numeric character references do not
come from the DTD, and MusicXML content uses nothing else.

- **Decision 6 — test fixtures and licensing.** **Synthetic fixtures plus a
  committed corpus, MuseScore only.** Hand-written MusicXML string literals cover
  the invariant and boundary tests, matching the existing `tests/` convention.
  Alongside them, four to six public-domain scores exported from MuseScore land
  in `tests/resources/musicxml/` with a `PRACTICE_TAKES_TEST_RESOURCES_DIR`
  compile definition, following the `PRACTICE_TAKES_SOURCE_DIR` precedent.
  Committing them is acceptable: the repertoire is public domain and the export
  files are ours.

  **Both premises were wrong in our favour, and the outcome is better than the
  decision.** "Export them ourselves" assumed no permissively-licensed corpus
  existed; OpenScore publishes 1,462 songs under CC0, and CPDL states a licence
  per edition. "MuseScore-dialect only" assumed no second notation program was
  available; CPDL's editors use whichever they own, so the corpus arrived with
  six — MuseScore, Finale, Sibelius, Dorico, Harmony Assistant, and PDFtoMusic
  Pro — without a notation program being installed at all.

  So the corpus is 36 scores rather than four to six, and task 8.5's
  MuseScore-only disclaimer was replaced by a statement of what is genuinely
  uncovered: percussion, orchestral scoring, and uncompressed documents.

  What the decision got right is the part that mattered: **licence provenance is
  checked per file, not inherited from a source.** Three candidates were
  rejected on the evidence of their own `<rights>` metadata — two were
  Non-Commercial or unlicensed arrangements of public-domain music, and the
  PDMX dataset was rejected wholesale because it rests on MuseScore uploader
  self-declaration with a 12% internal-metadata conflict rate by its own
  authors' measurement. Public-domain *music* is not a public-domain *file*:
  the composition, the arrangement, and the file each carry their own rights.

## Context

Practice Takes is a C++20 / JUCE 8 desktop application with a single
`CMakeLists.txt` that lists every source file explicitly, Catch2 v3 tests in a
`tests/` tree that mirrors `src/`, and a hard rule that nothing reachable from
the audio callback may allocate, lock, block, log, or touch files. There is no notation,
score, or MIDI code of any kind. The application's only structured music data is
a fractional MIDI-note value produced by `PitchDetector` from live microphone
input.

The repository has a consistent house pattern for anything that reads external
data, established by `BenchmarkRecordCodec`, `SettingsTransferCodec`, and
`WorkspaceCatalogCodec`: a scoped `enum class ...Status`, a
`struct ...Result { status; std::optional<Payload>; std::string error; }`,
`[[nodiscard]]` static decode functions, a `maximumDocumentBytes` cap with a
dedicated `tooLarge` status, and — in `WorkspaceCatalogCodec` — a
`loadedWithRecovery` status for partial success. That last one is directly
applicable: a MusicXML import that drops an unsupported construct is exactly
"loaded, with recovery". This change should reuse the pattern rather than invent
an error model.

There is no PIMPL anywhere in `src/`, only four named namespaces, and the most
recent feature area (`src/features/performance/`) uses a lowercase
`namespace performance`. Pure logic is separated from JUCE UI by file, not by
directory: `PitchDetector.h` includes `<juce_dsp/juce_dsp.h>` and is unit
tested; `TunerComponent.h` includes `<JuceHeader.h>` and is not. The test target
does not have `JuceHeader.h` on its include path, so **anything that must be
unit tested cannot include it** — a hard constraint on the model's headers.

MusicXML itself is the other half of the context. The characteristics that shape
this design:

- Two document roots exist, `score-partwise` (measures inside parts) and
  `score-timewise` (parts inside measures). Partwise is what every mainstream
  program exports.
- `.mxl` is a ZIP whose `META-INF/container.xml` names the root score document.
  The root is not reliably the first or largest entry, so the container must
  actually be read.
- `<divisions>` is per part and may be redeclared in any measure.
- Voices are written with a cursor: `<backup>` and `<forward>` move the write
  position within a measure, so events are not in time order in the file.
- A chord is a run of `<note>` elements where every note after the first carries
  `<chord/>` and consumes no additional time.
- `<tie>` is the sounding tie; `<notations><tied>` is its visual counterpart;
  `<slur>` is a different thing entirely and is frequently confused with a tie.
- A pickup measure is `<measure implicit="yes">` and legitimately does not fill
  its time signature. Over-full and under-full measures also occur in valid
  files from real programs.
- Lyrics carry a verse `number`, a `<syllabic>` value (single/begin/middle/end),
  and optional `<extend>` for melismas.
- Tempo appears as `<sound tempo="...">`, as `<metronome>`, or both, and the two
  can disagree.

## Goals / Non-Goals

**Goals:**

- One application-owned score representation that the renderer, the score tool,
  playback, and sessions can all read, and that none of them can mutate.
- A documented, testable MusicXML subset, with defined behaviour for everything
  outside it.
- Import that never crashes and never half-applies: it returns a complete score
  or a failure with a reason.
- Diagnostics a musician can act on, located by part, measure number as printed,
  and voice.
- Model headers unit-testable without a display and without `JuceHeader.h`.
- An explicit, written audio-thread contract for the score model, before any
  code exists that could violate it.

**Non-Goals:**

- Rendering, engraving, layout, spacing, or glyphs (#32).
- Navigation, zoom, part selection, cursors, overlays (#33).
- MIDI parsing (#34), playback, transport, synthesis (#35–#38), sessions (#39).
- Editing a score, transposing it, extracting parts, or writing MusicXML back
  out.
- Schema/DTD validation, or any network access during import.
- Round-trip fidelity. Unsupported constructs are reported, not preserved for
  re-export.
- Supporting `score-timewise`. Converting one root form to the other is an XSLT
  step, with its own dependency and its own test surface, and no mainstream
  program exports timewise. A timewise document is rejected with
  `unsupportedDocumentType` rather than misread.

## Decisions

These are the smaller choices I made directly, given the six above.

### Entities

The model is a tree of value types with a single root, built by an importer and
frozen. These live in `src/services/score/` under `namespace score`, per the
resolved decision 1:

- **`Score`** — root. Work title, movement title, composer/lyricist credits, the
  encoding software string (worth keeping: it is the first thing you want when a
  file misbehaves), the tick base and the source-to-tick scale factor, the part
  list, the tempo map, and the import diagnostics.
- **`Part`** — the MusicXML `<score-part>` id (kept verbatim, because #39 will
  need to name a selected part stably across sessions), display name,
  abbreviation, staff count, and its measures.
- **`Measure`** — the printed measure number as a *string* (MusicXML numbers are
  not integers: pickups are `"0"`, split bars are `"12a"`), a zero-based index,
  absolute start tick, nominal duration in ticks from the prevailing time
  signature, an implicit/pickup flag, the attribute changes that take effect at
  its start (clef per staff, key, time), repeat and ending markings as
  uninterpreted data (decision 3), and its voices.
- **`Voice`** — a MusicXML voice number within the part, and its events in
  ascending onset order after the `<backup>`/`<forward>` cursor has been
  resolved.
- **`Event`** — one of note, chord, or rest; staff number, onset tick relative
  to the measure start, duration in ticks, a grace flag (zero duration), a
  tuplet ratio when present, its notes, and lyric syllables.
- **`Note`** — one sounding note of an event: a `Pitch` plus its tie linkage.
  Tie linkage sits here rather than on the event because a chord may tie some of
  its notes and not others; see invariant 4.
- **`Pitch`** — step, alter, octave, **and** the derived MIDI note number. Both,
  always. A `Note` carries two of them, `written` and `sounding`, which differ
  only on a transposing instrument's part. The renderer needs the spelling to choose a staff line and an
  accidental; playback needs the number. Deriving the spelling back from a MIDI
  number is not possible, so a model that stores only the number cannot be
  rendered correctly, and a model that stores only the spelling makes every
  playback consumer redo the arithmetic.
- **`LyricSyllable`** — verse number, syllabic position, text, and an extend
  flag.
- **`Direction`** — a tempo or dynamic marking attached to a part, measure, and
  onset tick.
- **`TempoMap`** — ordered tempo entries by absolute tick, with tick-to-seconds
  and seconds-to-tick conversion. This is the piece decision 4 says to extract
  and share with MIDI import.
- **`Diagnostic`** — severity (error / unsupported / info), a location (part id,
  measure number, voice, event index — each optional), the offending element
  name, and a message.

### Invariants

These are what the model guarantees to its consumers, and what the unit tests
assert. They are the reason the model is worth having as a separate thing from
the parser.

1. Every part has the same number of measures, and measure `n` in every part
   has the same absolute start tick and the same nominal duration. Files that
   disagree are reconciled by padding with rests and a diagnostic is emitted.
2. Events within a voice are in ascending onset order, and no two events in a
   voice overlap. A chord is one event, not several.
3. Every duration is non-negative. Only grace notes have zero duration.
4. A tie chain has exactly one start and one stop, and every link points at a
   **note** that exists and sounds the same pitch. Notes rather than events: a
   chord may tie some of its notes and not others, and an event-level tie could
   not express that. Unmatched tie ends are dropped with a diagnostic rather
   than left dangling.
5. Every note's MIDI number is consistent with its spelling.
6. Part ids are unique and non-empty. A file with duplicate or missing ids gets
   generated ids and a diagnostic.
7. A voice's events do not extend past the measure's nominal duration, except in
   an implicit (pickup) measure, where they may be shorter. A bar whose content
   exceeds its time signature is **widened to fit**, with a diagnostic; the
   model's truncation is a backstop for hand-built scores only.

   This was originally "over-full measures are truncated", and real files
   overturned it. Renaissance editions carry `<time symbol="cut">2/2</time>` as
   a mensuration sign while every bar holds a breve — twice the signature.
   Truncating there silently halved every note of every part in several corpus
   scores. The rule that replaced it: an importer never destroys notes to
   satisfy a number it inferred. The signature is what the engraver wrote at the
   top of the staff; the notes are the music.
8. The tempo map is non-empty (a default of 120 BPM is inserted if the file
   declares none), sorted by tick, and has no duplicate ticks.
9. Diagnostics never reference a part, measure, or voice that is not in the
   score.
10. The score contains no engraving data: no coordinates, no fonts, no page or
    system breaks, no stem directions, no beam groupings. Anything positional is
    the renderer's to compute. This is #31's "independent of the rendering
    widget" criterion, stated as a checkable invariant.

### Ownership and threading

This is the part that has to be right on the first try, because the audio thread
is unforgiving and this repository already has a documented contract for it.

- **Import runs on a background thread.** Reading, unzipping, and DOM-parsing a
  score is unbounded work with file I/O; it cannot run on the message thread
  without freezing the UI, and obviously cannot run on the audio thread. The
  importer is a plain function on a caller-provided thread; it starts no threads
  of its own and touches no global state, so it is trivially testable
  synchronously.
- **The result crosses to the message thread as
  `std::shared_ptr<const Score>`.** `const` is the enforcement mechanism: a
  score is fully built before its first share and is never mutated afterwards,
  so any number of readers on any number of threads need no lock. This is why
  the model is value types with a builder rather than a mutable object graph.
- **A single owner holds the current score.** In this change that is the
  importer's caller (tests). When #39 lands, it becomes the session. Everything
  else holds a `shared_ptr<const Score>` copy, so a score stays alive as long as
  any reader is using it even if the session swaps in a different one.
- **The audio thread does not touch `Score`. Ever.** Not even to read it.
  `shared_ptr` copy is an atomic refcount operation, which is a lock-free but
  contended write, and the tree is pointer-chasing over heap nodes with
  unpredictable cache behaviour. Neither belongs in a callback that must be
  bounded. When playback arrives (#35–#38), the message thread must flatten the
  score into a preallocated, POD, contiguous event array and publish that to the
  audio thread using the existing pattern — the same shape as
  `AudioSampleFifo`, which is how `AudioInputService` already gets data across
  the boundary in the other direction. Stating this now is the point: the model
  is being designed so that nobody is ever tempted to read it from a callback
  because it looked convenient.
- **No PIMPL, no inheritance, no virtual dispatch in the model.** Matching the
  repository (`grep -rn "unique_ptr<Impl>" src` returns nothing) and keeping the
  types trivially copyable where possible.

### Import failure model

Reusing the repository's codec convention exactly:

- `MusicXmlImportStatus`: `imported`, `importedWithDiagnostics`, `notFound`,
  `unreadable`, `tooLarge`, `notMusicXml`, `malformedXml`,
  `unsupportedDocumentType`, `structurallyInvalid`.
- `MusicXmlImportResult { status; std::optional<Score> score; std::string error;
  std::vector<Diagnostic> diagnostics; }`.
- A score is present if and only if the status is `imported` or
  `importedWithDiagnostics`. There is no partial score on failure.
- Size caps, mirroring `SettingsTransferCodec::maximumDocumentBytes`: a cap on
  the source file, a cap on the uncompressed size of an `.mxl` entry, and a cap
  on the expansion ratio. A ZIP bomb is a plausible thing to receive from a
  stranger's score-sharing site and must produce `tooLarge`, not an OOM.
- The XML parser is reached only through one small adapter header, so decision 5
  is reversible in one file.

### Supported subset for the MVP

In, matching #31's requirement list: parts and multi-staff parts; measures;
notes, rests, chords; voices; ties; pitch with spelling; clef, key, and time
signature including mid-score changes; tempo from `<sound>` and `<metronome>`;
lyrics with verse numbers, syllabic position, and extends; dynamics as
directions; pickup measures; `score-partwise` documents; `.musicxml`, `.xml`,
and `.mxl` containers.

Recognised but dropped with a diagnostic, for the MVP: slurs, articulations,
ornaments, and other notations; grace notes beyond a zero-duration marker;
tuplet visual brackets (the ratio is kept, the bracket is not); chord symbols
and figured bass; percussion and unpitched notes; transposing-instrument
`<transpose>` (a real gap for wind parts, called out below); multi-measure
rests; `<print>` layout hints; cross-staff beaming; second-voice stem overrides.

Rejected outright: `score-timewise` documents (`unsupportedDocumentType`) and
anything that is not XML or a ZIP containing XML (`notMusicXml`).

Unrecognised elements are counted by element name and summarised in one
diagnostic per name, not one per occurrence — a Sibelius export will otherwise
produce thousands.

## Risks / Trade-offs

- **[Risk] `<backup>`/`<forward>` cursor handling is the most likely source of
  silent wrong output.** A mis-handled backup shifts an entire voice by a
  fraction of a bar, and nothing crashes — the score just plays wrong. Mitigated
  by invariant 7 (a voice may not exceed the measure) plus explicit tests for
  two- and four-voice measures, backup to a non-zero position, and forward past
  the end of a measure.
- **[Risk, resolved] Transposing instruments.** Originally dropped, which would
  have put every wind and brass part in the wrong key — silently, since nothing
  crashes. Opened as #127 and then fixed inside this change: `<transpose>` is
  read, and every note carries both its written and its sounding pitch.

  Storing both rather than storing the transposition and asking consumers to
  apply it is the point. The alternative works until one consumer forgets, and
  then a whole section plays a tone out with nothing to say so. The fields are
  named `written` and `sounding` rather than one of them being `pitch`, so
  choosing between them is unavoidable.
- **[Risk] Exporter dialects differ more than the format suggests.** Finale
  emits divisions and voice numbering unlike MuseScore's; Sibelius emits large
  volumes of layout elements. Decision 6 is the mitigation, and it only works if
  the corpus really does come from more than one program.
- **[Risk, resolved] The DOCTYPE was the sharpest edge in the format.** MusicXML
  files carry a DOCTYPE referencing an external DTD by URL, and may declare
  entities inline. Fetching the former on a file-open path would be an I/O stall
  and an XXE-shaped hole; expanding the latter is the classic XML denial of
  service, and `juce::XmlDocument` does expand inline entities and would follow
  a `SYSTEM` identifier given an input source. Resolved by **removing the
  DOCTYPE declaration outright before parsing**, which makes both impossible
  rather than bounded. Nothing is lost: the five predefined entities and numeric
  character references do not come from the DTD, and MusicXML content uses
  nothing else.
- **[Risk] Rounding when rescaling divisions (decision 2, option 1) accumulates
  within a measure.** Mitigated by rescaling with exact integer arithmetic where
  the division is exact, diagnosing where it is not, and asserting invariant 1
  and 7 per measure so drift cannot silently cross a barline.
- **[Trade-off] Structural validation instead of schema validation** means a
  file that violates the MusicXML XSD in a way we do not check will import
  anyway. Given that the alternative is a new dependency plus a network fetch on
  a file-open path, importing a technically-invalid file that renders correctly
  is the better failure.
- **[Trade-off] A DOM parse holds the whole document in memory.** A large
  orchestral score can be tens of megabytes of XML expanding to several times
  that as a DOM. Bounded by the size cap; a streaming parser would be a
  meaningful rewrite for a case an MVP for singers does not have.
- **[Risk] This change ships nothing a user can see.** The importer has no
  file-open command until #32 exists, so its only exercise is the test suite
  until then. That makes the test corpus the entire safety net, and it makes the
  change hard to "demo". Worth knowing before you review the diff and wonder
  where the feature is.

## Migration Plan

No data migration; nothing exists to migrate. The build order that keeps each
step independently reviewable:

1. Model types and invariants, with unit tests, and no parser. The model is
   constructible by hand, so its invariants are testable before any XML is read.
2. The XML/ZIP adapter and container resolution, tested against malformed,
   oversized, and non-MusicXML input. Still no MusicXML semantics.
3. The importer: structure (parts and measures), then time (divisions, backup,
   forward), then pitch and chords, then attributes and directions, then lyrics
   and ties. Each layer's tests pass before the next starts.
4. Diagnostics and the subset boundary, including the unrecognised-element
   summary.
5. The real-score corpus, and the documented subset.

Rollback is deleting the directory and its CMake entries. No existing file's
behaviour changes, so nothing can regress.

## Deviations from the spec deltas

Task 9.7 asks that the spec deltas be re-read against what was built, and that
any deviation be *recorded* rather than quietly edited away. Four, of which two
changed the spec and two did not.

**Changed the spec, because the implementation was right:**

- **`invalidContainer` was added to the failure-status list.** The container
  requirement already demanded "a status distinguishing it from a valid file",
  so the enumeration in the invalid-input requirement was simply incomplete. A
  `.mxl` with a broken manifest really is a container — saying "not MusicXML" of
  it would be less useful, not more.
- **Two scenarios were added for a document that yields no music.** This was an
  open question when the deltas were written, and it is now decided: zero
  *events* is a structural failure, zero *notes* is not. Writing only the first
  half into the spec would have left the second half as an accident of the
  implementation rather than a promise.

**Left unmet, and not papered over:**

- **"A large score is imported → the user interface remains responsive"** cannot
  be verified by this change. There is no file-open command until #32, so
  nothing exercises the importer from a UI at all. What *is* verified is the
  property the scenario rests on: `importMusicXmlFile` and
  `importMusicXmlDocument` start no threads, hold no global state, and touch
  nothing outside their arguments, and a test imports the same document
  repeatedly to hold that. The scenario becomes testable when #32 gives the
  importer a caller.
- **"Representative real-world scores import consistently" is not yet
  satisfied.** The corpus directory, the manifest format, the compile
  definition, and both corpus test cases exist; the corpus itself is empty,
  because exporting public-domain scores from MuseScore needs MuseScore, which is
  not installed on the development machine. `MusicXmlCorpusTests` reports the gap
  with a warning on every run rather than passing silently, and fails on any
  corpus file that has no pinned expectation — so adding files later cannot
  accidentally produce coverage that asserts nothing. Tasks 8.1 and 8.2 stay
  open.

## Open questions and assumptions

Separate from the six decisions above: these are smaller unknowns, and places
where I guessed and want the guess on the record.

**Assumptions I made without confirmation:**

- That #31 targets MusicXML 3.1/4.0 partwise files as exported by mainstream
  notation programs, not the full historical format. Nothing in the issue says a
  version.
- That "basic dynamics" in #31 means dynamics as text/marking data attached to a
  position, not as playback velocity. I treated them as directions.
- That "preserve unsupported elements for diagnostics where practical" means
  *report* them, not *retain* them for round-trip export. Retaining the source
  XML for re-export is a much larger commitment and I assumed it is not wanted.
- That singer-facing repertoire (SATB choral, solo voice with piano) is the
  priority, which is why transposing instruments are deferred.
- That the model has no user-visible strings requiring localisation, since there
  is no localisation anywhere in the repository today.

**Questions I could not answer from the repository or the issues:**

- Should the importer be able to report progress and be cancelled? A 30 MB score
  takes noticeable time. Cancellation is much easier to design in now than to
  retrofit, but it complicates the importer's signature and its tests, and no
  UI exists yet to drive it.
- What is a reasonable maximum score size? `SettingsTransferCodec` uses 4 MB,
  which is far too small for notation. I would guess 64 MB uncompressed, but
  that is a guess with no data behind it.
- ~~Should the import diagnostics be surfaced to users at all in this stage, or
  only logged?~~ **Answered: yes, and in a follow-up change.** #31 says
  "malformed files fail with useful messages" and this change gave them nowhere
  to appear — the importer's only caller is the test suite, so the criterion is
  unmet and the specified background-thread handoff has never been performed by
  the application. `openspec/changes/score-import-command/` adds the File menu,
  the chooser, the off-message-thread import, and the summary and diagnostics
  window. It deliberately draws no notation: that stays with #32, for the same
  reason this change landed the model before the renderer.
- Does the MVP need multi-movement or multi-`<work>` files, or is one score per
  file sufficient?
- Should `<part-group>` bracketing (which parts brace together on the page) be
  captured now even though only #32 uses it? It is cheap to capture and annoying
  to add later, but it is arguably engraving data, which invariant 10 says the
  model does not hold.
- ~~How should a file that imports with zero notes be treated?~~ **Answered
  (task 7.5): a structural failure, but stated over *events* rather than over
  notes.** The lean towards failure was right — zero events almost always means
  the parse went wrong. Stating it over notes would have been wrong: a movement
  of nothing but rests is unusual and entirely valid, and refusing it would
  reject a real score to catch a hypothetical one.
