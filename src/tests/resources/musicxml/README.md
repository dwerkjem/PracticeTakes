# Real-score corpus

Public-domain scores, exported by us, that `MusicXmlCorpusTests` imports and
checks. The synthetic fixtures in `src/tests/support/MusicXmlFixtures.h` fail
informatively when a rule breaks; these exist to catch the assumptions we did
not know we were making, which only real exporter output contains.

**The corpus is empty right now.** `MusicXmlCorpusTests` reports that it was
skipped rather than passing silently, so an empty corpus cannot be mistaken for
a passing one.

## Adding a score

1. Export a public-domain work from MuseScore as `.musicxml` or `.mxl` and put
   it in this directory. Aim for four to six files covering at least one
   multi-part vocal score (a Bach chorale, SATB) and one piano score (a Clementi
   sonatina).
2. Add a row to the provenance table below.
3. Add a line to `expectations.txt` pinning what the importer produces. The
   test fails on a file with no entry — a corpus file nobody asserts anything
   about is a file that cannot catch a regression.

To find the numbers for step 3, add the file and run the test; the failure
message prints the values it actually got.

## Licensing

Only public-domain repertoire, exported by us. The music is out of copyright and
the export file is ours, which sidesteps the redistribution question entirely
rather than relying on a licence we would have to track. Do not add a file
downloaded from a score-sharing site, whatever its stated licence.

## Provenance

| File | Work | Composer | Died | Exported from | Notes |
|---|---|---|---|---|---|
| _(none yet)_ | | | | | |

## What this corpus does not cover

**MuseScore exports only.** No second notation program is available, so this is
single-dialect coverage and must not be read as cross-dialect coverage. Finale
emits `<divisions>` values and voice numbering unlike MuseScore's, and Sibelius
emits far more layout elements. Neither is exercised here. Adding a Finale,
Sibelius, or Dorico export later is cheap and remains worth doing.
