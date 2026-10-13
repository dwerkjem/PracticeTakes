## Why

Opening a score today is a file dialog and nothing else. Nothing is remembered,
so the second session re-navigates to the same folder, and there is no way to
see what you have without opening each file in turn. For a practice
application that is backwards: the scores you are working on are a small,
stable, frequently revisited set, and they should be the first thing the
application can show you.

So: a **library** — a folder the user nominates, scanned for scores, presented
as a browsable list with a **preview of each** so a score is identifiable
without being opened.

## What Changes

- **A nominated library folder**, persisted in settings, scanned for
  `.musicxml`, `.xml` and `.mxl`. More than one folder, because repertoire
  arrives from more than one place.
- **A library view** listing every score found, with its title, composer and
  part list — metadata the model already carries in `ScoreMetadata` and
  `Part`, so listing costs an import and no new parsing.
- **A preview image per score**: the first system, rendered by the engraving
  layer that already exists, so a user recognises a piece by looking at it
  rather than by reading a filename. This is the first consumer of the layout
  engine outside the score view, and the first thing that renders a score
  without displaying it interactively.
- **A preview cache on disk**, keyed by file path plus content hash plus
  modification time, so a library of a hundred scores does not re-import and
  re-render on every launch. A changed file invalidates its own preview and
  nothing else.
- **Open from the library**, replacing the file dialog as the usual route in.
  The dialog stays, because a score outside the library must still be openable.
- **The library survives a missing folder.** A removable drive or a moved
  directory is normal; the entries stay listed and marked unavailable rather
  than being silently dropped, because silently dropping them loses the only
  record that they existed.

## Capabilities

### New Capabilities
- `score-library`: nominating folders, scanning them, what is listed about each
  score, how entries are identified across renames and moves, and what happens
  when a folder or file goes missing.
- `score-previews`: rendering a score's first system to an image without
  opening it, the cache that stores them, and what invalidates one.

### Modified Capabilities
- `score-import-command`: opening a score gains a second route, and the import
  path is now also driven by a background scan rather than only by a user
  choosing a file. Import currently assumes it is interactive — it opens a
  summary window — which a scan of ninety files must not do.
- `settings-transfer`: library folders are user settings and have to export,
  import and version like the rest. Paths are machine-specific, so a
  `.ptsettings` carried to another machine needs a stated rule rather than a
  silently broken list.

## Impact

- **New**: a library model (JUCE-free: entries, scanning policy, identity,
  staleness), a library UI surface, a preview renderer and its on-disk cache.
- **Changed**: settings persistence and its version; the score import path, to
  separate "import this file" from "import this file and tell the user about
  it"; `ScoreImportState`'s assumption that an import is always interactive.
- **Depends on**: the engraving layer (`engraved-notation-renderer`), which
  previews render through. Without it a preview would be a filename in a box.
- **Performance**: scanning and rendering happen off the message thread and
  publish finished values back to it, the same shape import already uses. The
  audio thread is not involved at any point.
- **Storage**: a cache directory outside the repository, under the platform's
  application-data location, with a stated size bound.

## Long-term compatibility

The cache is disposable by construction — deleting it costs a re-render and
never data — so its format may change freely, and it carries a version so an
old cache is discarded rather than misread. **The library index is not
disposable**: it records which folders a user nominated and must survive every
future version, so it is a versioned settings payload with the same
forward-compatibility rules as the rest of `settings-transfer`. A score file
itself is never written to by this change; the library only ever reads.
