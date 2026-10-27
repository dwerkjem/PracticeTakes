# Harness scores

Two MusicXML documents that exist to be photographed. An approved window state
in `src/application/testcontrol/ApprovedWindowStates.cpp` names one by file
name, the shell imports it on the way into that state, and the capture pass
takes a reference image of the score import summary window showing the result.

| File | Imports to | The picture it is for |
|---|---|---|
| `clean-import.musicxml` | `imported`, no diagnostics | Every metadata field the summary can show, a part on two staves, and the explicit "Nothing was dropped or repaired." line |
| `diagnostic-import.musicxml` | `importedWithDiagnostics` | All three severity groups at once, and six unrecognised elements sharing one message |

`src/tests/application/testcontrol/ApprovedScoreFixturesTests.cpp` pins both:
that each file a state names exists, and that it still imports to the status
and the shape of report its surface is about. Run

```bash
build/PracticeTakesTests "[.harness-score-report]" --success
```

to print the exact text each capture should contain.

## Why these are not in the corpus next door

`src/tests/resources/musicxml/` is real exporter output, licence-checked per
file and pinned row by row in its own manifest. It answers "what do real files
do"; these two answer "what does one window look like", which wants a *short*
and *stable* document rather than a representative one.

They are also not the generated corpus in `build/manual-score-import/`, which
`make_score_import_fixtures.py` rebuilds per run. A reference image whose input
is untracked is not a reference image, and a capture that differs because
someone regenerated a fixture is a false failure.

## Provenance

**Both files are hand-written for this repository**, by its own developer, and
carry no third-party content: no repertoire, no exporter output, nothing under
anyone else's licence. The pitches are a scale.

That was a decision with a real alternative. The defect these surfaces exist to
catch was found in a genuine MuseScore 4.7.3 export, whose report of seven
identical rows is the better *regression* picture — but a real export cannot go
in a public repository without provenance clearing all three layers the corpus
README lists, and it brings in a page of engraving data that makes the report
long and the image unstable. The same convention already applies to percussion:
`MusicXmlPercussionTests` works against a hand-written drum-kit fixture for the
same reason.

What that costs is covered elsewhere: the corpus next door is what imports real
exporter output, and `diagnostic-import.musicxml` reproduces the *shape* of the
MuseScore report — several unrecognised elements, one shared message, the
element name as the only thing telling the rows apart — rather than its bytes.

## Adding one

Only if a surface needs it. Each of these is two more images in every full
review grid, in every palette, forever.

1. Put the file here, using only constructs the importer supports if the
   surface is meant to be clean — anything outside
   `knownElements()` in `src/platform/score/musicxml/read/MusicXmlSubset.cpp`
   produces a diagnostic, which for a clean fixture is a silent change to what
   the picture shows.
2. Name it from a state in `ApprovedWindowStates.cpp`, appending the field
   positionally.
3. Pin what it imports to in `ApprovedScoreFixturesTests.cpp`, so a change in
   the importer fails a build rather than changing a reference image.
