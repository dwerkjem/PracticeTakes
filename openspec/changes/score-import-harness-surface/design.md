## Context

The test-control channel accepts a closed vocabulary. `ApprovedWindowStates.cpp`
says why in its own comment: a harness that has drifted fails loudly on an
unknown name, the set of verified surfaces is a reviewable list in one file, and
nothing is addressed by screen position. A state describes *what the application
should look like*, not the steps to get there, and the glue in
`MainComponentTestControl.cpp` decides how to establish it.

That glue is synchronous and returns `bool` — `false` meaning the state could
not be established, which the capture pass records as a failure. Settings and
feedback are reachable because a flag is enough: `showSettings()` opens a window
that has nothing to load.

The score import summary window is the first surface that depends on a
*document*. It has nothing to show until a file has been imported, and the
import is deliberately asynchronous — `ScoreImportJob` is a `juce::Thread` whose
result arrives on the message thread through `MessageManager::callAsync`. So the
state cannot be established by the time the glue returns, which is new: every
existing state is fully established synchronously.

Separately, `src/platform/score` is still headless infrastructure with one
consumer. `musicxml-import` landed the model and importer ahead of a renderer on
purpose, and this change must not become a reason to draw notation.

## Goals / Non-Goals

**Goals:**

- Make the summary window reachable by the capture pass, in both its clean and
  its diagnostic form, so the `Component` code that renders a report has a
  reference image and a reviewer's eye on it.
- Keep the vocabulary closed: a state names a committed fixture, never an
  arbitrary path supplied at run time.
- Reach the window through the real load path, so what the surface verifies is
  what a user gets.
- Complete task 5.5 of `score-import-command` and unblock its archive.

**Non-Goals:**

- A general `open-score <path>` command. Accepting any path at run time would
  make the surface list stop being a reviewable list of what is verified.
- Verifying the file chooser. It is a native dialog; the chooser stays outside
  the harness and outside this change, and the manual procedure in
  `docs/development/quality/score-import-manual-verification.md` keeps covering
  it.
- Any notation drawing. The window stays a summary.
- Reusing the generated corpus in `build/manual-score-import/`.

## Decisions

### 1. A state names a committed fixture, not a path

`ApprovedWindowState` gains one field naming a fixture under
`src/tests/resources/`; the glue resolves it to a file. The alternative — a
free-form path on the `open-state` command — was rejected because it moves the
decision about what gets verified out of the reviewable list and into whatever
the harness happens to pass, which is the property the closed vocabulary exists
to protect.

The field is **appended after the existing fields**. Every entry in the approved
list is initialised positionally, and the file already carries a warning that a
field inserted mid-list silently shifts every value after it in all forty
entries. That warning is the whole reason this is written down rather than left
to the implementer.

### 2. Fixtures are committed, not generated

The surfaces name two small files under `src/tests/resources/`: one that imports
cleanly, one that produces diagnostics of more than one severity.

The generated corpus is **not** reused, even though it exists and covers more
statuses. It lives in `build/`, is untracked, and is rebuilt per run — a
reference image whose input is untracked is not a reference image, and a
capture that differs because someone regenerated a fixture is a false failure.
`src/tests/resources/` already holds the MusicXML corpus, so this is the
established home. The two needs are genuinely different: the generated corpus
exists so a *person* can exercise every failure path, and these fixtures exist
so a *machine* can take the same picture twice.

### 3. The import runs through `ScoreImportJob`, and the surface waits with `warmup_seconds`

This is the real decision in the change, and the one with a cost.

The glue starts the import the way the File menu does and returns. The surface
then carries a `warmup_seconds` so the capture is taken after the result has
arrived. That field already exists for exactly this shape of problem — the tuner
and spectrogram surfaces use it so a capture shows a tool that has been
listening rather than one that just started.

Alternatives considered:

- **Import synchronously on the message thread.** Deterministic, and the
  fixtures are small enough that it would be milliseconds. Rejected because it
  bypasses `ScoreImportJob` entirely: the surface would verify a load path no
  user ever takes, and the one property most worth a picture — that the window
  renders a *finished* asynchronous result — would be the one property not
  exercised.
- **A `wait-until-idle` command on the channel.** Deterministic and reusable.
  Rejected for now as a larger widening than the change needs: it is a new
  command rather than a new field, and it invites every future surface to depend
  on it. Worth revisiting if a second document-dependent surface appears.

The cost is that capture timing becomes load-bearing for these two surfaces,
where for every existing surface it is not. Mitigated below.

### 4. The surfaces name the window, and are captured once

Like the settings and feedback surfaces, each names the summary window as the
window to capture. Without it the pass photographs whichever window the window
manager lists first, which is the main one — a mistake a real run already made,
when the settings surface came back byte-identical to the empty shell.

For the same reason those surfaces are captured once rather than at every
resolution: the geometry command resizes the *main* window, so sweeping would
produce the same image under several labels.

### 5. A state naming a missing fixture fails loudly

The existing test that every approved state's tool names resolve is extended to
the new field: a state whose fixture is absent fails the test, rather than
producing a capture of an empty window that a reviewer might approve. This is
the same bet the channel makes everywhere else — fail on a drifted name rather
than verify something that no longer exists.

## Risks / Trade-offs

**A slow or loaded machine captures mid-import, and the reference image is of
the in-progress state.** → Three things reduce it to a visible rather than a
silent failure: the fixtures are small enough to import in milliseconds while
the warmup is seconds; the window's in-progress state is visibly distinct, so a
mis-timed capture is obviously wrong in the review grid rather than subtly
wrong; and `ui-capture-and-review` already treats a surface that cannot be
captured as a recorded failure. If it proves flaky in practice, decision 3's
`wait-until-idle` alternative is the fix, and this is the evidence that would
justify it.

**The channel's vocabulary grows, which `score-import-command` explicitly
declined to do.** → Accepted deliberately, and narrowed: a field naming a
committed fixture, not a command accepting a path. The surface list stays the
reviewable record. This is why the work is its own proposal rather than a commit
on that change's branch.

**A positionally-initialised list gains a field.** → Appended, never inserted,
per decision 1. The existing warning in the file is the mitigation; the risk is
that an implementer in a hurry puts it next to the logically-related
`settingsOpen` instead, which compiles and silently corrupts forty entries.

**Two more images in every full review grid.** → Accepted. They are captured
once rather than per resolution, so it is two images per palette, and the
alternative is a window with no verification at all.

**The fixtures drift from what the importer accepts.** → They are ordinary
MusicXML under `src/tests/resources/`, which the existing corpus tests already
import; if the importer stops accepting one, those tests fail before the harness
does.

## Migration Plan

No migration. The new field defaults to empty, so all forty existing states and
every existing surface behave exactly as before. The change is additive and
reverts cleanly by removing the two states, the two surfaces, and the field.

## Open Questions

1. **Should the diagnostic fixture be a real MuseScore export or a hand-written
   document?** A real export is what found the defect that motivated this, and
   carries idioms nobody thought to write by hand. A hand-written one contains
   exactly the constructs under test and nothing else, which is the stated
   convention for the importer's fixtures. Leaning hand-written for the surface,
   since a reference image wants a short, stable report rather than a
   representative one — but the MuseScore export's seven-identical-rows report
   is the better regression picture.
2. **Does the clean-import surface earn its place?** The diagnostic one covers
   strictly more of the renderer. The argument for both is that "Nothing was
   dropped or repaired." is a distinct branch that exists precisely because an
   empty list and a failure to report are indistinguishable on screen — which is
   worth a picture of its own.
3. **Should a failed import get a surface too?** The failure path renders a
   different layout, and `notFound` is trivially reachable by naming a fixture
   that does not exist — except that decision 5 makes a missing fixture a test
   failure. Reaching it would need an explicit "expect this to fail" marker on
   the state, which is more vocabulary again.
