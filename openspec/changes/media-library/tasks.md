## 1. The library on disk

- [x] 1.1 `library::LibraryLayout`: the folder, `Songs/`, `Takes/`, the default
      under Music, a take's folder name from its time and title, and the
      copy-on-open rule (inside the library or not, same bytes reused, a
      numbered name for a clash). Tests against a temporary folder.
- [x] 1.2 `library::TakeManifest`: the struct, encode and decode, versioning,
      bounds; a take to a manifest and back, the lifecycle replayed to its end
      state. Tests.

## 2. Saved takes

- [x] 2.1 `library::TakeFiles`: write a take's folder (manifest, FLAC, score
      copy) and read one back into a `take::Take`, the clock table rebuilt.
      Round-trip tests, keyboard and microphone, a missing score copy, a newer
      version refused.
- [x] 2.2 `ScoreImportState` records the open score's file; `Take` gains its
      score file and `libraryFolder`; `TakeRecorder::keep` keeps a loaded take
      without notifying it as new. Tests.
- [x] 2.3 `library::TakeSaver`, a thread that writes each newly kept take, and
      the shell wiring: a kept take is saved, a loaded one is not, a failure is
      reported. Tests.

## 3. Songs

- [x] 3.1 Open Score copies into `Songs/` once the import succeeds, and opens
      from the copy. Tests of the rule (1.1) and the shell wiring.
- [x] 3.2 `library::SongListing`: scan `Songs/` and the samples folder, the
      index cache keyed by name, size and time, unreadable files listed. A
      background scanner. Tests.
- [x] 3.3 `library::TakeListing`: scan `Takes/` manifests, newest first,
      unreadable ones listed. Tests.

## 4. The Library window

- [x] 4.1 File → Library…, the window, the Songs and Takes tabs, the buttons,
      the folder line with Change…, the library folder in `AppSettings` (local,
      not in bundles). Strings by key.
- [x] 4.2 Review a saved take: load on a worker, publish its score, keep and
      review it, open Take Review.
- [x] 4.3 Remove and delete to the bin, with confirmation; Show in Folder;
      Refresh; "This song only".
- [x] 4.4 `--test-control` uses a temporary library; a `library` capture state
      with songs and takes; captures read in both themes.
      Done as two states, `library-songs` and `library-takes`, built in code
      on `empty` and `take-review-sung` rather than spelled out positionally.
      The scratch library is emptied at the start of every state, and a take
      the harness keeps through `TakeRecorder::keep` takes the open score's
      file, so it saves with its score copy like a recorded one.

## 5. Previews (from `score-library`)

- [ ] 5.1 Render a song's first system to an image through the engraving
      layer, off the message thread, and show it in the Songs list.
- [ ] 5.2 The preview cache on disk, keyed by path, content hash and time,
      versioned and disposable.

## 6. Other folders (from `score-library`)

- [ ] 6.1 Folders the user nominates, listed beside `Songs/`, kept as a local
      setting, marked unavailable when missing rather than dropped.

## 7. Docs

- [ ] 7.1 The wiki (a Library page, File menu, Take Review), the Features row,
      ARCHITECTURE.md § Library, the take format in
      `docs/development/formats/`.
