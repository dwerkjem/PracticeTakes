# Verifying score import by hand

Tasks 5.1–5.4 of the `score-import-command` change are manual, and not for want
of effort: the file chooser is a native dialog, and the test-control channel can
click an approved target but cannot drive a GTK file picker. Nothing in
`PracticeTakesTests` opens a real file through the real menu, so these four
properties are checked by a person or not at all.

What *is* automated is the corpus. `tools/scripts/quality/make_score_import_fixtures.py`
writes one file per `MusicXmlImportStatus`, so a run does not begin with twenty
minutes of hand-building malformed XML.

## Before you start

```bash
./tools/scripts/build/build-and-run.sh --build-only
python3 tools/scripts/quality/make_score_import_fixtures.py
```

The corpus lands in `build/manual-score-import/`. It is regenerated from
scratch each time, so a half-finished earlier run cannot leave a stale file
behind.

| File | Should produce |
|---|---|
| `00-clean.musicxml` | `imported` — and the explicit "nothing was dropped" line |
| `01-not-found.musicxml` | `notFound` — a dangling symlink, the only way to reach this through a chooser |
| `02-unreadable.musicxml` | `unreadable` — mode 000 |
| `03-too-large-ratio.mxl` | `tooLarge` — past the 200:1 expansion ratio |
| `04-not-musicxml.musicxml` | `notMusicXml` — well-formed XML rooted at `<html>` |
| `04b-empty.musicxml` | `notMusicXml` — an empty file |
| `05-malformed-xml.musicxml` | `malformedXml` |
| `06-invalid-container-no-manifest.mxl` | `invalidContainer` — no `META-INF/container.xml` |
| `06b-invalid-container-missing-entry.mxl` | `invalidContainer` — manifest names a missing entry |
| `07-unsupported-document-type.musicxml` | `unsupportedDocumentType` — `score-timewise` |
| `08-structurally-invalid.musicxml` | `structurallyInvalid` — a measure with no events |
| `09-lossy.musicxml` | `importedWithDiagnostics` — five diagnostics |
| `10-large.mxl` | `imported`, after ~5.5 s |

Two caveats worth knowing before a result confuses you:

- **Do not run the application as root.** Root ignores the permission bits, so
  `02-unreadable.musicxml` imports perfectly and task 5.3 silently loses a case.
  The generator warns if *it* is run as root; it cannot tell how you launch the
  application.
- **The status mapping is not covered by any test.** The generator's Python
  tests check that the files are well-formed *as fixtures* — the symlink
  dangles, the ratio bomb exceeds the cap — but only the C++ importer decides
  which status a file produces, and nothing asserts that it still does. If a
  file in the table comes back with the wrong status, suspect the table before
  the application, and re-check it against
  `src/tests/platform/score/musicxml/container/MusicXmlContainerTests.cpp`.

## 5.1 — The interface stays responsive during a large import

This is the property the whole background-thread design exists for, and nothing
has ever exercised it.

1. Open the tuner, docked, and let it run so it is visibly drawing.
2. **File → Open Score** → `10-large.mxl`.
3. While it imports (~5.5 s), confirm all of:
   - the tuner keeps responding to sound and its graph keeps advancing;
   - the window drags and resizes;
   - the summary window says an import is in progress and names the file;
   - no "not responding" shade from the window manager.

A frozen tuner graph is the failure this is looking for. If 5.5 s is too quick
to judge, raise `measures` in the generator's `large()` — but read its docstring
first, because the decompressed size and the DOM both scale with it.

## 5.2 — Quitting mid-import neither crashes nor hangs

The risk is `stopThread(-1)`: the import never polls `threadShouldExit()`, so
quitting waits for the parse rather than deadlining it. That is the deliberate
choice (a killed parser mid-DOM-parse is a corrupted heap), and the cost is a
quit that can take a few seconds.

1. **File → Open Score** → `10-large.mxl`.
2. Quit immediately — window close, then repeat with `Ctrl+Q`.
3. Confirm the process exits within a few seconds, with no crash dialog and no
   zombie. `pgrep PracticeTakes` after the window disappears should find nothing.

Run it twice, because a `SafePointer` that is not doing its job may only bite on
the race where the import finishes *just* as the window goes.

## 5.3 — Each failure status reads differently

Open each failing file in the table and read the message. The question is not
whether there is an error but whether a user could act on it: "invalid
container" is a status, "the compressed score's manifest names
`somewhere-else.xml`, which is not in the file" is an explanation.

The importer's own messages are already distinct — verified directly against
all eight failure fixtures — so what is under test here is the **window**: that
it shows the importer's message rather than paraphrasing or flattening it, and
that nothing is truncated.

After a failure, confirm the previously opened score is still open. That rule is
unit-tested in `ScoreImportState`, but this is where you see it hold end to end:
open `00-clean.musicxml`, then `05-malformed-xml.musicxml`, and the clean score
should still be the current one.

## 5.4 — Diagnostics say what was dropped, and where

Open `09-lossy.musicxml`. It carries `<harmony>`, `<slur>`, `<articulations>`,
`<ornaments>`, a `<technical>` fingering repeated four times, and one element
that is not MusicXML at all.

Confirm:
- diagnostics are grouped by severity, and the groups are labelled;
- each one names a musical location, not a byte offset or a line number;
- the repeated fingering shows an occurrence count rather than four
  near-identical rows;
- the list scrolls rather than truncating — and compare against
  `00-clean.musicxml`, which must say *explicitly* that nothing was dropped. An
  empty list and a failure to report are indistinguishable on screen, which is
  the whole reason that line exists.

## 5.5 — Add the window to the manual GUI harness

Still open, and it needs a decision rather than a tester. A surface in
`tools/scripts/testing_suite/surfaces.py` names an approved state from
`src/application/testcontrol/ApprovedWindowStates.cpp`, and there is deliberately
no way for the harness to compose a state of its own. No approved state can open
the summary window today, because reaching it means importing a file, and
importing a file means a path the channel has no way to supply.

The two honest options:

- **An attended-only surface.** State `empty`, an `instruction` telling the
  tester to open a named fixture by hand, and every question `behavioural`.
  Needs no C++ change. But the unattended capture pass does not read
  `instruction` — only `attend.py` does — so the surface would still be
  captured, photographing the main window and adding a meaningless image to
  every review grid.
- **A `scoreImportPath` field on `ApprovedWindowState`.** The same shape as the
  existing `settingsOpen` and `feedbackOpen` flags, appended after `toolView`
  because the list is initialised positionally. This makes the window capturable
  like the settings and feedback windows, and in exchange widens the channel's
  vocabulary — which is what the `score-import-command` change explicitly
  declined to do, and what makes it arguably its own proposal.

The second is the one that actually gets the window into the harness. It is not
in #206.

## Related

- [The testing suite](TESTING_SUITE.md) — the surfaces, the attended pass, and
  the run record
- [The supported MusicXML subset](../formats/musicxml-subset.md) — what each
  status means and every diagnostic the importer emits
