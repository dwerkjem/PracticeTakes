## Why

Opening a score today is a file dialog and nothing else. Nothing is remembered,
so the second session re-navigates to the same folder, and there is no way to
see what you have without opening each file in turn. Takes are worse off: they
live in memory, fifty to a session, and closing the application loses every
one. For a practice application that is backwards. The scores you are working
on are a small, stable, often revisited set, and the takes you recorded of them
are the record of your progress. Both should be the first thing the application
can show you.

So: a **media library**, one folder on disk where songs and takes go, and a
**Library** window, opened from the File menu, that lists both.

This change began as `score-library`, which proposed the songs half: nominated
folders, a browsable list and a preview per score. The maintainer asked for a
library of songs *and* takes, opened from File (2026-11-10). That proposal is
folded in here rather than kept beside it. Its decisions stand where they still
fit, and its nominated folders and previews are tiers of this change rather than
dropped (design.md decisions 1 and 9).

## What Changes

- **A library folder.** It defaults to `Practice Takes` under the user's Music
  folder, can be changed from the Library window, and holds `Songs/` and `Takes/`. The
  application owns what it writes there; the user can see, back up and move all
  of it, because it is ordinary files.
- **Songs go into it.** Opening a score from outside the library copies the
  file into `Songs/` and opens the copy, so it is there next time. A score
  already in the library is opened where it is. The shipped sample, Mahler's
  "Die zwei blauen Augen", is a built-in entry, listed from the `Samples` folder
  it is already written to, always present and not removable.
- **Takes go into it.** Every take the recorder keeps (completed, stopped early
  or failed, never cancelled) is saved to `Takes/` when it ends, on a
  background thread. A take is a folder: what was recorded (the microphone as
  FLAC, the keyboard as events), when, at what tempo scale and reference pitch,
  and a copy of the score it was played against. So a take still reads
  correctly after its song is edited, renamed or deleted.
- **A Library window**, from File → Library…, with two lists. **Songs** gives
  title, composer, parts and when each was added, and can open, add, remove or
  show the file. **Takes** gives when, which song, how long, what was used
  (microphone, keyboard) and how it ended, and can review, delete or show the
  file. Reviewing a saved take opens its song and judges it with today's
  settings, as reviewing a take of this session does.
- **Removing is to the bin.** Remove and Delete move files to the system's
  rubbish bin rather than erasing them, because a take is a recording that
  cannot be made again.
- **The library survives a missing folder.** If the folder is on a drive that
  is not attached, the window says so and the application carries on without
  it. Nothing is created in its place, and no take is lost silently: one that
  cannot be saved is reported.
- **Later tiers, carried from `score-library`:** a preview of each song's first
  system, rendered by the engraving layer, with a disposable cache; and further
  folders the user nominates, listed beside the library's own `Songs/`.

## Capabilities

### New Capabilities
- `media-library`: the library folder and its layout, what is listed about a
  song and a take, the built-in sample, the Library window and its commands,
  and what happens when the folder or a file goes missing.
- `saved-takes`: when a take is saved, what a saved take holds, its file
  format and version, and loading one back for review.

### Modified Capabilities
- `score-import-command`: opening a score from outside the library first puts a
  copy in it, and File gains Library….
- `settings-transfer`: the library folder is a setting of this machine, not one
  that travels in a `.ptsettings` bundle.

## Impact

- **New**: `src/platform/library` (the library's layout, the song and take
  listings, the take file codec, and a writer and loader that run off the
  message thread), and `src/application/shell/ui/library` (the Library window).
- **Changed**: the File menu and the Open Score route; `TakeRecorder` gains a
  way to keep a take loaded from disk without saving it again; `AppSettings`
  gains the library folder; the Take Review tool can review a take loaded from
  the library.
- **Audio thread**: not involved. Saving reads a take that has already ended,
  on its own thread; loading decodes on its own thread and hands the finished
  take to the message thread.
- **Disk**: a three-minute microphone take is about 10 MB as 24-bit FLAC at
  44.1 kHz. Nothing is deleted automatically; the user deletes takes they no
  longer want.

## Long-term compatibility

A saved take is user data and must survive every future version. Its
manifest carries a format version; a reader refuses a newer major version
rather than misreading it, and reads every older one. The score copy and the
FLAC audio are standard formats, readable without this application. The song
listing cache, like the preview cache to come, is disposable: deleting it costs
a rescan and never data. Song files are only ever copied into the library, never
written to in place.
