# The supported MusicXML subset

What the importer in `src/platform/score/musicxml/` reads, what it recognises
and deliberately drops, and what it refuses outright.

This document is part of the contract, not a summary of it. The renderer (#32)
may rely on anything listed as imported being present in the model, and on
nothing else being there. If the importer changes, this changes with it in the
same pull request.

## Accepted input

| Extension | What it is |
|---|---|
| `.musicxml` | The document |
| `.xml` | The document |
| `.mxl` | A ZIP container whose `META-INF/container.xml` names the root document |

Whether a file is a container is decided by **its first four bytes, not its
extension**. Exporters really do produce `.xml` files that are containers and
`.mxl` files that are plain documents, and the content is what has to be parsed
either way.

Only `score-partwise` documents are read. See [Refused outright](#refused-outright).

### Limits

A file arriving from a stranger's score-sharing site is a plausible attack, so
three limits stand in front of the parser. They live as named constants in
`MusicXmlImportResult.h`, beside the code that enforces them.

| Limit | Value | Why |
|---|---|---|
| Source file | 64 MB | Bounds the DOM parse, which holds the whole document in memory by design. A large orchestral score is a few megabytes. |
| Decompressed container entry | 256 MB | Higher than the on-disk cap because XML compresses very well and a legitimate `.mxl` is routinely a tenth of its expanded size. |
| Expansion ratio | 200:1 | Real MusicXML lands around 10:1 to 20:1; a ZIP bomb is 1000:1 and up. |

The entry's *declared* uncompressed size is checked first, so an archive
declaring a 4 GB entry is refused without a byte being decompressed.

### The DOCTYPE is removed before parsing

A MusicXML document declares an external DTD by URL and may declare entities
inline. The importer deletes the whole declaration before the parser sees it.

That makes two attacks impossible rather than merely bounded: an external
reference cannot be resolved (no I/O stall, no XXE) and an entity cannot be
expanded (no billion laughs). Nothing is lost — the five predefined entities and
numeric character references do not come from the DTD, and MusicXML content uses
nothing else.

**No network access occurs at any point during an import.**

## Imported

Everything here reaches the model and is covered by tests.

### Structure

- Parts, including multi-staff parts (`<staves>`), with the `<score-part>`
  identifier kept verbatim, display name, and abbreviation.
- Measures, with the **printed measure number as a string** — MusicXML numbers
  are not integers: a pickup is `"0"` and a split bar is `"12a"`.
- Pickup measures (`<measure implicit="yes">`), which are exempt from the
  measure-duration bound.
- Voices, resolved through the `<backup>`/`<forward>` write cursor.
- Work title, movement title, composer, lyricist, and the encoding software
  string.

### Time

- `<divisions>` per part, **including a change partway through the score**,
  rescaled into the model's fixed 3840 ticks per quarter note. A conversion that
  is not exact rounds and emits a diagnostic rather than drifting silently.
- Time signatures, including mid-score changes, which set each measure's
  nominal duration — **unless the bar's own content is longer**, in which case
  the bar is widened to fit and the disagreement is reported. Renaissance
  editions routinely carry `<time symbol="cut">` as a *mensuration sign* while
  every bar holds a breve; trusting the signature there halves every note in
  the piece. An importer never destroys notes to satisfy a number it inferred.

### Notes

- Pitched notes, with `<step>`, `<alter>`, and `<octave>` kept **and** the
  sounding MIDI number derived. Both, always — a C-sharp and a D-flat are the
  same number and different staff positions.
- `<transpose>`, including `<octave-change>`, `<double/>`, and a per-staff
  `number`, and including a change partway through the score. Every note carries
  **both** its written and its sounding pitch: the renderer draws `written`,
  playback and pitch matching use `sounding`. Spelling is transposed
  diatonically as well as chromatically, so a B-flat clarinet's written C
  sounds a B-flat rather than an A-sharp.
- Rests, including whole-measure rests (`<rest measure="yes">`), whose duration
  comes from the bar when the file omits it.
- Chords: a run of `<note>` elements where every note after the first carries
  `<chord/>` becomes **one event with several notes**, consuming one duration.
  A chord tone consumes no time whether or not it has a pitch the model can
  store — an unpitched percussion chord still costs one duration, not several.
- Ties (`<tie>`), **per note rather than per chord**. A chord may tie some of
  its notes and not others — a pianist holding the bass while the upper voices
  move — and the model records that faithfully. Matched by sounding pitch, so a
  G-sharp may tie to an A-flat across a barline. Unmatched ends are dropped with
  a diagnostic.
- Grace notes, as zero-duration events that do not advance the cursor.
- Tuplet **ratios** from `<time-modification>`.

### Attributes and directions

- Clefs per staff, including `<clef-octave-change>`, and mid-score changes.
- Key signatures (`<fifths>`, `<mode>`), including mid-score changes.
- Tempo, from `<sound tempo="...">` and from `<metronome>`. **When they
  disagree, `<sound>` wins**: it is the explicit playback value, while the
  metronome marking is what is engraved and may be stale or set for appearance.
  The disagreement is reported. A `<metronome>` beat unit is converted to
  quarter notes per minute, dots included, so "dotted quarter = 60" is 90.
- Dynamics, as directions attached to a part, measure, and position — notation,
  not playback velocity.
- Repeat barlines, ending (volta) numbers, and repeat counts, **captured but
  never interpreted**. The model is as-written: measures appear once, in source
  order, and nothing expands a repeat into a playback order.

### Lyrics

Verse number, syllabic position (`single`/`begin`/`middle`/`end`), text, and the
`<extend>` melisma flag. A syllable with neither text nor an extender is
dropped; a bare `<extend/>` is kept, because it continues the previous syllable.

## Recognised and dropped

Each of these produces **one diagnostic per element name with an occurrence
count**, not one per occurrence — a piano score has a slur on nearly every
phrase, and one diagnostic each would bury every real finding.

| Element | Note |
|---|---|
| `<slur>` | A phrasing mark, not a sounding tie |
| `<articulations>` | Staccato, accent, tenuto, … |
| `<ornaments>` | Trills, mordents, turns |
| `<technical>` | Fingering, bowing, fret positions |
| `<harmony>` | Chord symbols |
| `<figured-bass>` | |
| `<unpitched>` | Percussion |
| `<multiple-rest>` | Imported as ordinary rests |
| `<print>` | Page and system layout is the renderer's to decide |
| `<defaults>` | Fonts, page size — engraving, not score |
| `<part-group>` | Instrument bracketing — engraving, not score |

`<notations><tied>` is the engraved counterpart of a tie and creates no link;
the sounding `<tie>` does.

## Unrecognised elements

Anything the importer has never heard of is **counted by name and summarised in
one diagnostic per name**. A Sibelius export contains thousands of layout
elements; reporting each would be useless.

This is how a dialect nobody anticipated announces itself rather than vanishing.

## Refused outright

| Status | Cause |
|---|---|
| `unsupportedDocumentType` | A `score-timewise` document. Converting between the two roots is an XSLT step with its own dependency and test surface, and no mainstream program exports timewise. |
| `notMusicXml` | Well-formed XML whose root is neither `score-partwise` nor `score-timewise`; an empty document; a file that is neither XML nor a ZIP holding XML. |
| `malformedXml` | Not well-formed XML. |
| `invalidContainer` | A ZIP with no `META-INF/container.xml`, an unreadable manifest, or a manifest naming an entry the container does not hold. |
| `tooLarge` | Any of the three limits above. |
| `notFound`, `unreadable` | The path does not exist, or could not be opened. |
| `structurallyInvalid` | A `score-partwise` document with no parts, or one that yields no notes **and** no rests. Zero events cannot be a real score; zero *notes* can, so a movement of nothing but rests imports normally. |

## What "validation" means here

**Our structural rules, not schema validation.** There is no XSD or DTD
validation, because it would need a validating parser and, worse, the DTD is
referenced by an external URL — a network call on a file-open path.

So a file that violates the MusicXML schema in a way this importer does not
check will import anyway. Given that the alternative is a new dependency plus a
network fetch, importing a technically-invalid file that renders correctly is
the better failure.

## Diagnostics

A diagnostic names a **musical** location — part, printed measure number, voice,
position — rather than a line and column. That is what a musician can act on:
they can open the score and look at bar 12. It is also what survives the file
being re-exported.

Three severities:

- `unsupported` — the file asked for something the model cannot represent, and
  the score now differs from what the file said.
- `repaired` — a structural problem was fixed to satisfy an invariant: an
  over-full measure truncated, a dangling tie dropped, a part identifier
  generated.
- `info` — an observation, chiefly the unrecognised-element summaries.

**No amount of dropped or unrecognised content turns a successful import into a
failure.** A score is either complete and satisfying every model invariant, or
absent; there is no partial score on failure.

## Related

- [Architecture](../architecture/ARCHITECTURE.md) § Score model — ownership,
  immutability, and why the audio thread never reads a score.
- `openspec/changes/musicxml-import/design.md` — why the subset is drawn here,
  and the six decisions behind the model.
