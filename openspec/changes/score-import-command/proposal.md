## Why

The `musicxml-import` change added a MusicXML importer and a normalized score
model, and left both reachable only from the test suite. There is no file-open
command, no menu entry, and no code path from a user action to
`importMusicXmlFile`. The feature exists and cannot be used.

Two concrete consequences, not just an aesthetic gap:

- **#31 asks that "malformed files fail with useful messages", and the messages
  have nowhere to appear.** The importer produces a status, an error string, and
  a list of located diagnostics for every file it reads. Nothing displays any of
  it. That acceptance criterion is unmet, and `design.md` for the previous change
  records "should the import diagnostics be surfaced to users at all in this
  stage, or only logged?" as an open question. This change answers it: yes.
- **The specified threading has never been run.** The previous change specifies
  that import happens off the message thread and the finished score crosses back
  as `std::shared_ptr<const Score>`. Every test calls the importer synchronously,
  so the handoff the design is built around has no exercise anywhere. A
  background-thread contract that nothing performs is a contract that is probably
  wrong.

Waiting for the engraved renderer (#32) to supply the entry point is the thing
`musicxml-import` was written to avoid in the first place: if the first caller of
the model is a paint routine, the model becomes whatever the paint routine
needed.

## What Changes

- Add a **File menu** to the application shell with an **Open Score…** entry.
  There is no File menu today; the shell's only menus are per-tool workspace
  menus.
- Open a file chooser filtered to `.musicxml`, `.xml`, and `.mxl`, following the
  `juce::FileChooser::launchAsync` plus `Component::SafePointer` pattern already
  used by settings import.
- **Run the import on a background thread** and return the finished
  `std::shared_ptr<const Score>` to the message thread, so a large score cannot
  block the UI. This is the first real use of the handoff `musicxml-import`
  specified.
- Show a **score summary window** for a successful import: work and movement
  title, composer, the encoding software the file was written by, part count and
  each part's name and staff count, measure count, total musical length in bars
  and in seconds, and the tempo in force at the start.
- Show the **diagnostics** the import produced, grouped by severity, each with
  its musical location (part, printed measure number, voice) and its occurrence
  count. A clean import says so rather than showing an empty list.
- Show a **failure reason** for a file that could not be imported, distinguishing
  the importer's statuses rather than reporting a generic error.
- Hold the imported score as the application's **current score**, owned by the
  shell, so that #32, #33, and #39 have an owner to read from rather than each
  inventing one.
- Keep the window **non-modal and re-openable**, so a user can compare a
  diagnostic against the file and import again without restarting.

Deliberately **not** in scope:

- Any drawing, engraving, layout, or notation glyphs (#32).
- Score navigation, zoom, part selection, cursors, or the target-note overlay
  (#33).
- Playback or anything reaching the audio thread (#35–#38).
- Persisting the opened score, or remembering it across launches (#39).
- Progress reporting and cancellation during import. Recorded as an open question
  in the previous change and still deferred; the size cap bounds the wait.
- Drag-and-drop, recent-files lists, and command-line file arguments.

## Capabilities

### New Capabilities

- `score-import-command`: the user-facing route from a MusicXML file on disk to
  an imported score — the menu command, the file chooser, the off-message-thread
  import, the summary and diagnostics presented to the user, the failure
  reporting, and the shell's ownership of the current score.

### Modified Capabilities

None. `musicxml-import` and `normalized-score-model` are still unarchived
changes rather than published specs, and this change alters neither's
requirements: it consumes the importer exactly as specified and adds no
behaviour to it.

## Impact

- **`src/application/shell`** — gains a File menu, the file chooser, the
  background import job, and ownership of the current score. `MainComponent` is
  already split across files by responsibility, so this follows that split rather
  than growing an existing file.
- **New UI surface** — a score summary window under `shell/ui/`, alongside the
  existing settings and feedback windows.
- **New threading in the shell** — the shell has not run a background job before.
  `FeedbackComponent`'s `juce::Thread` subclass plus
  `juce::MessageManager::callAsync` is the precedent to follow, including its
  shutdown handling: the job must not outlive the component that launched it.
- **`src/platform/score/musicxml`** — read, not modified. If a summary needs
  something the model does not expose, that is a finding worth recording rather
  than a reason to reach past the public entry points.
- **`CMakeLists.txt`** — new sources on both the app and test targets.
- **Behaviour change for existing users** — the first one this line of work has:
  a File menu appears, and a MusicXML file can be opened. Nothing else changes,
  and no existing tool is affected.
- **Downstream** — #32 and #33 read the current score this change establishes an
  owner for; #39 replaces that owner with the session.
