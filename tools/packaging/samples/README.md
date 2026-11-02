# Sample score

Practice Takes ships one score, so that someone who has just installed it can
see the Score View working before finding a MusicXML file of their own. It is
offered by **File > Open sample score** and by the button in the empty window.

- `mahler-die-zwei-blauen-augen.mxl`: **Gustav Mahler, "Die zwei blauen
  Augen von meinem Schatz"**, the fourth of the *Lieder eines fahrenden
  Gesellen* (1884–85), for voice and piano. Words and music are Mahler's own.

## Why this one

It was chosen for the amount of notation in its 68 bars, because a sample that
only shows the easy cases proves nothing about the renderer. Counted as
elements in the file, across both parts, so a key change written on both
staves counts twice:

| | |
|---|---|
| Key signatures | 6 |
| Time signatures | 14 |
| Tuplet notes | 33 |
| Grace notes | 4 |
| Ties | 26 |
| Dynamics | 16 |
| Accidentals | 115 |
| Lyrics | 170 syllables |
| Staves | voice, plus piano on a braced grand staff |

Not all of that is drawn yet. The Score View tells the reader what it leaves
out, and this score is the one to watch improve as it learns more.

## Licence

All three layers the corpus README asks about clear:

| Layer | Status |
|---|---|
| The composition | Public domain: Mahler died in 1911 and wrote the words himself |
| The arrangement | None: this is the original voice-and-piano version |
| The file | CC0 1.0, from the [OpenScore Lieder Corpus](https://github.com/OpenScore/Lieder); the file carries `<rights>OpenScore (CC0)</rights>` |

So it can be shipped, modified and redistributed with no notice. OpenScore asks
for, but does not require, credit: this is the OpenScore Lieder Corpus's
transcription of a public-domain edition, source
`musescore.com/openscore-lieder-corpus/scores/5026316`.

## One file, checked twice

It is **byte-for-byte the same file** as
`src/tests/resources/musicxml/mahler-4-die-zwei-blauen-augen-von-meinem-schatz.mxl`,
where its licence was checked when it joined the real-score corpus.
`BundledSampleScoreTests` fails if the two diverge, so the copy shipped to
users is always the one whose provenance is on record, and whose import the
corpus pins row by row.

## How it ships

`CMakeLists.txt` embeds it as the `PracticeTakesSamples` binary-data target,
the same mechanism as the fonts. On first use the application writes it into a
`Samples` folder under its own application-data folder and opens it from there
through the ordinary import path, so the import summary names a real file. It
writes the file again if the copy there is missing or no longer matches.

That folder is also where the score library (`openspec/changes/score-library`)
picks it up, as a built-in entry beside the folders a user nominates.
