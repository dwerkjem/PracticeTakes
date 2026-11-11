## Context

A score reaches the application through File → Open Score…, a chooser, and
`ScoreImportJob`, which imports on its own thread and hands the result to
`MainComponent::finishScoreImport`. The sample is written into
`bundled_sample::defaultFolder()` and opened the same way. Takes are made by
`take::TakeRecorder`, which keeps the last fifty of the session in memory
(`TakeRecorder::takes()`) and judges one at a time for the Take Review tool.
Nothing about either is remembered between sessions.

## Decisions

### 1. One library folder the application writes, not folders it watches

The library is one folder, `<Music>/Practice Takes` by default, with `Songs/`
and `Takes/` inside it. The maintainer described it as the place "where songs
and takes will go", which is a place things are put, not an index over files
left wherever they were. It also gives a take somewhere to be saved, which
watched folders do not.

`score-library` proposed nominated folders instead. They are kept as the last
tier (6), listed beside `Songs/`, because repertoire does arrive from more than
one place, but they are not the library's foundation.

The Music folder rather than the application-data folder, because these are the
user's recordings and scores, which they should be able to find, back up and
carry to another machine. The folder is shown at the foot of the Library window
with a Change… button there (Settings has no general panel to hold it), and
changing it moves nothing: the old folder is left as it is, and pointing back at
it finds everything again.

### 2. Opening a score from elsewhere copies it into `Songs/`

File → Open Score… on a file outside the library copies it into `Songs/` and
imports the copy. A file already under the library folder, the sample included,
is imported where it is. So a score opened once is in the library from then on,
without an "add to library" step to forget.

- **A copy, not a reference.** A reference breaks when the original is moved,
  which `score-library` called normal for a removable drive. A score is a few
  kilobytes, so the copy costs nothing.
- **The same file is not copied twice.** If `Songs/` already holds a file with
  the same bytes, that file is opened instead. A different file with the same
  name is copied as `Name (2).musicxml`, and so on.
- **Copied only if it imports.** The file is imported from where it is first,
  and copied only once that succeeds, so a malformed file does not clutter the
  library. The copy is then what `ScoreImportState` records as the open file.

### 3. A take is saved when it is kept, as a folder

Every take `TakeRecorder` keeps is written to
`Takes/<yyyy-mm-dd HH-mm-ss> <song title>/`. Kept means completed, stopped
early or failed (`take::isKept`); a cancelled take was nothing the player
meant, and is not saved. The folder holds:

- `take.json`, the manifest (decision 4);
- `microphone.flac`, the microphone, mono 24-bit at the take's sample rate,
  when the take used it;
- `score.musicxml` or `score.mxl`, a copy of the file the take's score was
  imported from.

**Its own copy of the score**, because a take is judged against the score it
was played against. A song that is edited, renamed or deleted afterwards must
not change what an old take says, or stop it opening. It costs a few kilobytes
per take.

**FLAC at 24 bits** rather than WAV or float. It is lossless for anything a
microphone delivers, half the size of WAV, and JUCE reads and writes it. The
audio is the analysis tools' signal after gain and mute, the same samples the
judging read.

The writer (`library::TakeSaver`) runs on its own thread. The recorder's
listener hands it the kept take's `shared_ptr<const Take>`, which is immutable,
so there is nothing to lock. A save that fails, from a full disk or a missing
folder, is reported once in the Library window's status line and in the log.
It is not retried, and it is not silent.

### 4. The manifest

`take.json` is JSON. `formatVersion` is `"1.0"`: a reader refuses a newer major
version and reads every older one, as `.ptsettings` does. It holds:

- `recorded`, an ISO 8601 local time with offset;
- `scoreTitle`, `scoreFile`, `startTick`, `scalePercent`, `sampleRate`,
  `referenceHz`;
- `inputLatencyFrames`, `outputLatencyFrames`;
- `usedMicrophone`, `usedKeyboard`;
- `audioStart`, `countInFrames`, `lastFrame`;
- `stoppedInBar`, absent when it did not stop early;
- `state` (`completed`, `stoppedEarly` or `failed`) and `failureReason`;
- `loopStartTick` and `loopEndTick`, equal when there was no loop;
- `keyboard`, an array of `[frame, kind, channel, note, velocity, pedal]`.

The clock table is not stored: `transport::buildTable` rebuilds it exactly from
the score, scale, sample rate and loop. The lifecycle is not stored either, only
its end state; loading replays the events that reach it.

The codec, `library::TakeManifest`, is a struct with encode and decode to and
from a JSON string, built on `juce::JSON` and depending on `juce_core` only, as
`BundledSampleScore` does. So its tests need no window and no device, and every
rule about the format (versions, missing fields, out-of-range values) is
asserted there. Decoding bounds the document's size, as `SettingsTransferCodec`
does.

### 5. Loading a take publishes its song

Reviewing a saved take reads it on a worker thread (`library::TakeLoader`): the
manifest, the FLAC, and the score copy through the ordinary importer. The
message thread then:

1. publishes that score as the current score, through
   `ScoreImportState::apply`, so the Score View shows what the take was played
   against;
2. gives the recorder the take with that score (`TakeRecorder::keep`), marked
   as already in the library so it is not saved again (`Take::libraryFolder`);
3. reviews it, and opens the Take Review tool.

The target follows the score as it does for any import. The import summary is
not shown, because the reader asked to review a take, not to open a file.

### 6. Listing songs reads each song once

The Songs list names each file's title, composer and parts, which means
importing it. A background scan imports every file in `Songs/` and in the
samples folder, and caches the result in `Songs/.library-index.json`, keyed by
file name, size and modification time. A file whose key still matches is listed
from the cache, so a library of a hundred scores costs one directory listing on
the second open. The cache is disposable: a missing or unreadable one is
rebuilt.

A file that fails to import is still listed, by its file name and marked as
unreadable, rather than hidden. It is in the folder, and the reader should be
able to see and remove it.

### 7. The Library window

The window is a `DocumentWindow` like Settings: one instance, owned by
`MainComponent`, themed by `applyAppearanceToOpenWindows`. It has two tabs:

- **Songs**: title, composer, parts, and date added. The buttons are Open, Add
  Songs…, Remove, and Show in Folder. Double-click opens.
- **Takes**: date and time, song, length, inputs, and how it ended, newest
  first. The buttons are Review, Delete, and Show in Folder. Double-click
  reviews.

Selecting a song and switching to Takes does not filter; a "This song only"
toggle does. It is off by default, because the first question is usually "what
did I record last?".

Remove and Delete move to the rubbish bin (`juce::File::moveToTrash`). Each asks
for confirmation, naming what goes. The built-in sample has no Remove.

The window refreshes when it opens, when a take is saved, and when a song is
added. It does not watch the folder for changes made outside the application;
**Refresh** is there for that.

### 8. `src/platform/library`, below the shell

The library's rules (its layout, the copy-on-open naming, the song listing and
its cache, the take manifest, saving and loading) live in
`src/platform/library`. They depend on `juce_core` and `juce_audio_formats`
(for FLAC), and on `platform/take` and `platform/score`. Nothing there knows a
window exists. The shell owns one `library::MediaLibrary` and the Library
window. No tool reaches the library: the Take Review tool reviews a loaded take
through `TakeRecorder`, as it does any other.

### 9. Previews and nominated folders are later tiers

`score-library`'s preview of each song's first system, rendered by the engraving
layer and cached on disk, is tier 5. Its nominated folders are tier 6. Neither
is needed for the library to hold songs and takes, and both build on the listing
from decision 6. Previews key their cache as `score-library` said: path, content
hash and modification time.

### 10. Captures never touch the user's library

`--test-control` puts the library in a fresh temporary folder for that process,
so a capture run neither reads the user's songs nor saves its synthetic takes
among theirs. A state that wants a library with something in it fills that
folder itself.

### 11. Decided while building

- **The folder's Change… is in the Library window**, at its foot beside the
  folder it shows, because Settings has no general panel to hold it.
- **A save reports itself in the Library window's status line**: "Take saved"
  with its folder, or why not. A failure is also said once a session in an
  alert, because the window may be closed and a take that is not saved is gone
  when the application quits. A capture instance never shows the alert.
- **The background work is one worker.** `library::BackgroundWork` runs one job
  at a time (a listing, a load, an Add Songs import) and hands each result back
  to the message thread. Saving has its own thread (`TakeSaver`), so a long
  listing never delays a take reaching the disk.
- **"This song only" matches by title.** A take records its song's title and
  carries its own score copy, not a link to the song's file, so a song and its
  takes are matched by title.
- **The listing marks the sample "(built in)" and an unreadable file "(cannot
  be read)"** in its title cell, rather than with an icon, so the state is read
  and not guessed.

## Risks / Trade-offs

- **Disk use grows with practice.** Nothing is deleted automatically, which is
  deliberate: deleting a recording behind the user's back is worse than a full
  disk. The Takes list shows each take's size, and a later change can add a
  limit if asked.
- **Copying on open** means a score edited in its original place does not
  change the library copy. That is the point (a reference would break), but a
  reader who edits scores elsewhere needs to open the edited file again. It is
  then copied as a new song unless its bytes are unchanged.
- **The listing imports every song** on its first scan. A hundred-score library
  is a few seconds on a worker thread, once.

## Open Questions

None that block the first tiers. Whether the Library should also open by
itself at launch, as a start page, is the maintainer's call once it exists.
