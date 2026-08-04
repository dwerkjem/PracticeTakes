## Context

The `musicxml-import` change added `src/platform/score/` (the model) and
`src/platform/score/musicxml/` (the importer). Both are complete, tested, and
unreachable: `grep -rn "importMusicXmlFile" src --include=*.cpp` finds only
tests. This change builds the first caller.

What the previous change already decided, and this one must honour rather than
revisit:

- **A score is `std::shared_ptr<const Score>`.** Fully built before it is first
  shared, never mutated after. `const` is what makes any number of readers on any
  number of threads safe without a lock.
- **Import is a plain function on the caller's thread.** `importMusicXmlFile`
  starts no threads, holds no global state, and touches nothing outside its
  arguments. Choosing the thread is the caller's job — which is this change.
- **The audio thread never touches a `Score`.** Not even to read it. Nothing here
  goes near the audio thread, and nothing here should make it look easy to.
- **Import returns a status, an error string, and diagnostics.** Failure is a
  `MusicXmlImportStatus`, not an exception. A score is present if and only if the
  status is `imported` or `importedWithDiagnostics`.

What the shell currently is:

- `MainComponent` is the application shell. It is deliberately split across files
  by responsibility — `shell/ui/{main_window,feedback,settings,workspace}` and
  `shell/state/{appearance,audio}` — rather than defined in one large source
  file. A new responsibility means a new file, not a bigger existing one.
- **There is no File menu.** The only menus are per-tool popup menus built in
  `MainComponentWorkspaceMenu.cpp`. This change adds the first application-level
  menu.
- **The shell has never run a background job.** `AudioInputService` owns the audio
  thread and `FeedbackComponent` owns a submission thread, but `MainComponent`
  itself has only ever done message-thread work.

Three precedents to copy rather than reinvent:

| Need | Precedent |
|---|---|
| Asynchronous file chooser | `MainComponentSettings.cpp::importSettings` — `juce::FileChooser::launchAsync` with a `Component::SafePointer` captured in the callback |
| Background job returning to the message thread | `FeedbackComponent` — a `juce::Thread` subclass, `juce::MessageManager::callAsync` to deliver, `signalThreadShouldExit` plus `stopThread` in the destructor |
| Menu construction | `MainComponentWorkspaceMenu.cpp` — `juce::PopupMenu` with named item ids |

## Goals / Non-Goals

**Goals:**

- A user can open a MusicXML file and see what the importer made of it.
- Malformed files produce a message a user can act on, satisfying #31's
  criterion that has so far had nowhere to appear.
- The off-message-thread handoff the previous change specified is actually
  performed, so a design nothing exercised becomes a design something does.
- The shell owns the current score, giving #32, #33, and #39 an owner to read
  from rather than three inventing their own.
- The summary is honest about what was dropped, so a user who opens a file with
  unsupported content learns that from the application rather than from a
  wrong-looking score later.

**Non-Goals:**

- Drawing anything musical. No staves, no noteheads, no glyphs (#32).
- Navigation, zoom, part selection, cursors, overlays (#33).
- Playback, transport, or anything reaching the audio thread (#35–#38).
- Persistence of any kind — no recent files, no reopening on launch (#39).
- Progress reporting and cancellation. Deferred again, deliberately; see Open
  Questions.
- Drag-and-drop and command-line file arguments.
- Changing the importer or the model. If the summary wants something they do not
  expose, that is a finding to record, not a licence to reach past the public
  entry points.

## Decisions

### Where the code lives

`src/application/shell/ui/score/`, matching the existing `feedback`, `settings`,
and `workspace` siblings. Three responsibilities, three files rather than one:

- the menu and the file chooser (the command),
- the background import job,
- the summary window.

The score model and importer stay in `src/platform/score/`. Nothing musical moves
into the shell.

**Alternative considered:** putting this in `src/features/notation/`, where the
renderer will live. Rejected for now — a File menu is shell furniture, not a
tool, and `src/features/*` is for things that open in the tool workspace. When
#33's score tool arrives, the *tool* goes in `features` and keeps using the
shell's current score.

### How the background import works

A `juce::Thread` subclass owned by `MainComponent`, following
`FeedbackComponent`'s shape exactly: `run()` calls `importMusicXmlFile`, then
`juce::MessageManager::callAsync` delivers the result; the destructor calls
`signalThreadShouldExit()` and `stopThread(timeout)`.

**Alternative considered:** `juce::ThreadPool`. Rejected because it is a second
threading idiom in a repository that already has one, for a job that is never
concurrent with itself.

**Alternative considered:** `std::async`/`std::jthread`. Rejected for the same
reason plus a worse shutdown story — the JUCE idiom's `stopThread` is what makes
"the window closed mid-import" a solved problem rather than a race.

**One import at a time.** Invoking Open Score while an import is running does not
start a second one. The spec requires only that two competing imports do not run
and that the user is not left without feedback; the simplest satisfying
behaviour is to keep the existing job and tell the user what is being read.

**The result must not reach a destroyed component.** `Component::SafePointer` in
the delivery lambda, matching `importSettings`. This is the failure mode most
likely to survive review and show up as an intermittent crash on quit, so it is
a named task and a named test.

### What the summary shows, and why that list

The score model exposes exactly what a user needs to sanity-check an import
against the page in front of them: part names and staff counts, measure count,
total length, tempo, and the metadata. Total length is shown **in bars and in
seconds** — bars because that is what a musician counts, seconds because it is
the one number that proves the tempo map was read.

`Score::metadata.encodingSoftware` is shown prominently. It looks like trivia
and is not: exporter dialects differ more than the format suggests, and it is the
first thing anyone wants to know when a file misbehaves.

**A clean import says so.** An empty diagnostics list and a failure to report
diagnostics look identical on screen. The distinction between the `imported` and
`importedWithDiagnostics` statuses is worth nothing if the UI collapses them.

### How failures are presented

The importer already distinguishes nine statuses and supplies a message written
for a user rather than a log. The window shows that message. It does **not**
paraphrase, re-map, or collapse statuses into "could not open file" — doing so
would discard the work the previous change did to make the failures specific.

### Ownership of the current score

`MainComponent` holds a `std::shared_ptr<const Score>`, empty until the first
successful import. Handed out by value, so a reader's score stays alive and
unchanged even after a different score is opened. This is the ownership the
previous change's design describes, with the shell standing in for the session
until #39 replaces it.

### Testability

The pure logic — turning a `MusicXmlImportResult` into the text the window shows
— is split out of the `Component` so it can be unit tested without a display,
following the `WorkspaceLayoutState` precedent. That covers the summary
formatting, the bars-and-seconds arithmetic, the diagnostic grouping, and every
failure status's message.

The window, the menu, and the thread are `Component`/`Thread` code and stay
outside `PracticeTakesTests`, consistent with the rest of the shell. The manual
GUI verification harness is where they get exercised.

## Risks / Trade-offs

- **[Risk] The import job outliving the component that launched it.** The
  classic JUCE shutdown crash: a background thread calls back into a destroyed
  component. → `stopThread` in the destructor plus `Component::SafePointer` in the
  delivery lambda, and a test that destroys the owner mid-import.
- **[Risk] This is the shell's first background job, so there is no local
  pattern to follow and the wrong one is easy to invent.** → Follow
  `FeedbackComponent` deliberately rather than incidentally, and say so in the
  code, so the next background job in the shell has two consistent examples
  instead of two different ones.
- **[Risk] A summary window is a tempting place to start drawing notation.** The
  whole point of `musicxml-import` was to land the model before the renderer so
  the model is not shaped by a paint routine. A summary that grows a little staff
  preview undoes that. → Non-goal stated here, and the window holds no musical
  drawing at all.
- **[Trade-off] No progress or cancellation.** A 64 MB score will block the
  window with no feedback beyond "importing". Accepted: the size cap bounds the
  wait, no real vocal or piano score approaches it, and cancellation complicates
  the importer's signature for a case the MVP does not have. Revisit when a real
  file is actually slow.
- **[Trade-off] The window is informational, so it will look thin.** It reports
  rather than does. That is correct for this stage — it exists so the importer has
  a caller and malformed files have a voice, not because a summary is a feature
  anyone asked for. It is likely to be replaced outright by #33's score tool.
- **[Risk] Diagnostics may be numerous even after aggregation.** A score with
  many distinct unsupported constructs produces one entry each. → Group by
  severity, show the count, and make the list scrollable rather than truncating
  silently; a truncated list that does not say it is truncated is worse than a
  long one.

## Migration Plan

No data migration. The build order that keeps each step independently
reviewable:

1. The presentation logic, as pure functions over `MusicXmlImportResult`, with
   unit tests. No UI, no threading.
2. The background import job and the shell's current-score ownership, including
   shutdown handling.
3. The menu and the file chooser.
4. The summary window, assembled from the first three.

Rollback is deleting `shell/ui/score/`, the current-score member, and the File
menu. Nothing existing changes behaviour, so nothing can regress.

## Open Questions

- **Should the File menu hold anything else?** A File menu with one item invites
  Quit, Preferences, and Recent Files. Settings already has its own route, and
  adding entries that duplicate it would be worse than a short menu. Leaving it
  at one item for now, but it is a question the next change in this area
  inherits.
- **Should an import failure be a dialog rather than the summary window?** A
  window that shows either a summary or an error is one surface to build and one
  to find; a dialog for errors is more conventional. Going with the single window
  because the diagnostics on a *successful* import matter as much as the error on
  a failed one, and splitting them would put the two halves of "what happened to
  my file" in different places.
- **Progress and cancellation** — deferred again, and worth re-asking the first
  time someone opens a file that takes long enough to notice.
- **Does the current score belong on `MainComponent` at all, or on a small
  owner of its own?** `MainComponent` already owns a great deal. A dedicated
  holder would be tidier and is what #39 will want. Keeping it on
  `MainComponent` for now on the grounds that one `shared_ptr` member is not
  worth a new type, and #39 is the change that should decide.
