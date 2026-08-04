# Real-score corpus

Thirty-six scores imported end to end by `MusicXmlCorpusTests`, from two
sources:

- **Twenty songs** from the [OpenScore Lieder Corpus](https://github.com/OpenScore/Lieder)
  — CC0, voice and piano, Beethoven to Webern.
- **Sixteen choral works** from [CPDL](https://www.cpdl.org) — Public Domain or
  CC-BY, one to eleven parts, exported by six different notation programs.

The synthetic fixtures in `src/tests/support/MusicXmlFixtures.h` fail
informatively when a rule breaks, because each contains exactly the construct
under test. These exist to catch the assumptions we did not know we were
making, which only real exporter output contains. They earned that on arrival:
the first run against real files found two bugs the synthetic fixtures could
never have found — unpitched chord tones advancing the voice cursor, and ties
being flattened from per-note to per-chord.

## Licence

**OpenScore files:** CC0 1.0 Universal. The repository is CC0 in full and each
file carries `<rights>OpenScore (CC0)</rights>` in its own metadata. OpenScore
requests — but does not require — credit, so: these are transcriptions by the
OpenScore Lieder Corpus from public-domain editions on IMSLP.

**CPDL files:** each edition's licence was read from its work page before
download, and only `Public Domain` and `Creative Commons Attribution` editions
were taken. Anything Non-Commercial, No-Derivatives, or Share-Alike was
excluded — NC is incompatible with this project's BSD-3-Clause terms, which
permit commercial use.

## `expectations.txt` is the corpus manifest

A score is in the corpus **if and only if it has a row in `expectations.txt`**.
The test iterates the manifest, not the directory.

That distinction is deliberate. This directory may also hold scores that are
useful to test against locally but cannot be committed, because the repertoire
is under copyright and this is a public repository. `.gitignore` ignores every
score file here by default; the CC0 ones are force-added. Local extras are
still run through the model-invariant checks — they are real files and the
invariants hold for any score — and reported as unpinned, but they cannot fail
a build that does not have them.

## Adding a score

**Check the licence before anything else.** Three layers all have to clear:

| Layer | Cleared by |
|---|---|
| The composition | Composer long dead, or explicit release |
| The **arrangement** | An arrangement of a public-domain work is a *new* copyright |
| The **file** | The transcriber's terms, and the host's |

"Mozart died in 1791" only clears the first. Read the file's own metadata:

```bash
unzip -p score.mxl '*.xml' | grep -E '<rights>|<source>|type="arranger"'
```

An absent `<rights>` means **unknown**, not free. A `<creator type="arranger">`
naming a living person means layer 2 has not cleared. Anything with `NC` or
`ND` in its licence is incompatible with this project's BSD-3-Clause terms.

Then:

1. Put the file in this directory and force-add it: `git add -f <file>`.
2. Add a row to the provenance table below.
3. Add a row to `expectations.txt`. Run
   `PracticeTakesTests "[.corpus-report]" --success` to get the numbers and a
   breakdown of the diagnostics behind them.

Good sources: [OpenScore](https://github.com/OpenScore/Lieder) (CC0),
[CPDL](https://www.cpdl.org) (per-score licence, good for SATB), and
[IMSLP](https://imslp.org) if you engrave it yourself — then the file is
unambiguously ours.

## Provenance

All twenty from OpenScore Lieder, CC0, transcribed from IMSLP editions. Voice
and piano unless noted.

| File | Composer | Died | Work |
|---|---|---|---|
| `beach-1-o-mistress-mine.mxl` | Amy Beach | 1944 | *3 Shakespeare Songs*, Op. 37 no. 1 |
| `beethoven-3-vom-tode.mxl` | Ludwig van Beethoven | 1827 | *6 Lieder*, Op. 48 no. 3 |
| `berg-1-nacht.mxl` | Alban Berg | 1935 | *7 frühe Lieder* no. 1 |
| `boulanger-5-au-pied-de-mon-lit.mxl` | Lili Boulanger | 1918 | *Clairières dans le ciel* no. 5 |
| `brahms-6-du-sprichst-dass-ich-mich-taeuschte.mxl` | Johannes Brahms | 1897 | *9 Lieder und Gesänge*, Op. 32 no. 6 |
| `debussy-2-il-pleure-dans-mon-coeur.mxl` | Claude Debussy | 1918 | *Ariettes oubliées* no. 2 |
| `faure-4-a-clymene.mxl` | Gabriel Fauré | 1924 | *Cinq mélodies*, Op. 58 no. 4 |
| `hensel-2-vorwurf.mxl` | Fanny Hensel | 1847 | *5 Lieder*, Op. 10 no. 2 |
| `joplin-please-say-you-will.mxl` | Scott Joplin | 1917 | *Please Say You Will* |
| `liliuokalani-aloha-oe.mxl` | Queen Liliʻuokalani | 1917 | *Aloha ʻOe* — four parts |
| `liszt-2-pace-non-trovo.mxl` | Franz Liszt | 1886 | *3 sonetti di Petrarca*, S.270b no. 2 — three parts |
| `mahler-4-die-zwei-blauen-augen-von-meinem-schatz.mxl` | Gustav Mahler | 1911 | *Lieder eines fahrenden Gesellen* no. 4 |
| `mendelssohn-3-die-liebende-schreibt-mwv-k-66.mxl` | Felix Mendelssohn | 1847 | *6 Gesänge*, Op. 86 no. 3 |
| `satie-5-chanson-du-chat.mxl` | Erik Satie | 1925 | *Ludions* no. 5 |
| `schubert-12-am-meer.mxl` | Franz Schubert | 1828 | *Schwanengesang*, D.957 no. 12 |
| `schumann-1-was-weinst-du-bluemlein.mxl` | **Clara** Schumann | 1896 | *6 Lieder*, Op. 23 no. 1 |
| `schumann-4-die-stille.mxl` | **Robert** Schumann | 1856 | *Liederkreis*, Op. 39 no. 4 |
| `strauss-2-caecilie.mxl` | Richard Strauss | 1949 | *4 Lieder*, Op. 27 no. 2 |
| `webern-5-ihr-tratet-zu-dem-herde.mxl` | Anton Webern | 1945 | *5 Lieder nach Gedichten von Stefan George*, Op. 4 no. 5 |
| `wolf-9-der-schreckenberger.mxl` | Hugo Wolf | 1903 | *Eichendorff-Lieder* no. 9 |

The range is deliberate. Beethoven through Webern covers a century and a half
of notation practice; Berg and Webern exercise dense chromatic spelling, where
keeping the spelling *and* the sounding number matters most; Liszt is the
longest and most texturally complex; Joplin and Liliʻuokalani are idioms
unlike the German lied.

## What this corpus covers

| Axis | Coverage |
|---|---|
| Exporters | MuseScore 2/3, Finale v25–v27, Sibelius 7–22, Dorico 5, Harmony Assistant, PDFtoMusic Pro |
| Parts | 1, 2, 4, 5, 6, 7, 10, 11 |
| SATB | Open score (`holyoke-acworth`, four parts) **and closed score** (`holyoke-acworth-2v`, two staves × two voices via `<backup>` — the case cursor handling is hardest for) |
| Double choir | `gabrieli-ave-regina` (10 parts), `usper-la-battaglia-a-8` (11 parts, 1836 bars) |
| Eras | Renaissance polyphony, shape-note hymnody, nineteenth-century lieder, early modernism |

Each of the three non-MuseScore dialects found bugs on arrival; see the git
history for `readMeasure`.

## What this corpus still does not cover

- **No percussion.** CPDL is a choral library and has none, and the
  public-domain MuseScore-derived datasets rely on uploader self-declaration
  this project does not accept. Percussion is covered instead by
  `MusicXmlPercussionTests`, against a drum-kit fixture reproduced from a real
  export — see the note at the top of that file. If a permissively-licensed
  real percussion score turns up, it belongs here.
- **No orchestral score.** Nothing here mixes transposing winds, percussion,
  and strings in one file.
- **No `.musicxml`/`.xml` file.** Every corpus score is a compressed `.mxl`;
  uncompressed input is covered only by the synthetic fixtures.
