## Why

A score is imported once and is then frozen. If a note is wrong — a mis-entered
pitch, a bad duration, an octave out — the only recourse is to leave the
application, fix the file in a notation editor, and import it again. For a
practice tool that is the wrong loop: the errors you find are found *while
practising*, and the fix should be where you found it.

So the score becomes editable in the application, with the page re-rendering as
it changes, and the result written back to the `.mxl` the score came from.

## What Changes

- **An edit model**: a set of operations over a score — change a pitch, change a
  duration, add or delete an event, move an event between staves — each one a
  value that can be applied, described and undone.
- **Copy-on-write, not mutation.** This is the load-bearing decision and it is
  taken deliberately. `Score.h` makes a score `shared_ptr<const Score>` and says
  why: a score is fully built before its first share and never mutated after, so
  any number of readers on any number of threads need no lock, and `const` is
  the entire enforcement of that. Making `Score` mutable would delete that
  guarantee and every reader would need a lock it does not have today. So an
  edit **builds a new immutable score and republishes it**, and every existing
  reader keeps the one it holds until it next looks — which is exactly the
  arrangement they are already written for, because pointer identity is already
  how they detect a change.
- **Undo and redo**, as a stack of applied operations. Required, not a
  nicety: an editing surface without undo is one a user cannot safely try
  anything in.
- **Live re-render.** The score view already re-lays-out when the score pointer
  changes, so a republished score redraws with no change to the view at all.
  That is the payoff of the existing design and this change should need
  nothing new there.
- **A MusicXML writer**, so an edit can go back to disk. The repository has an
  importer and no exporter; this is the larger half of the work, and
  `docs/development/formats/musicxml-subset.md` is the contract it must satisfy
  in reverse.
- **Fidelity is bounded and stated.** The model is a *normalised* subset: the
  importer drops constructs with diagnostics, so a naive write-back would
  silently delete whatever the model never held. Writing back SHALL preserve the
  parts of the source document the model does not represent, or refuse and say
  so. Silently discarding a user's engraving is the failure mode this change is
  most able to cause and least able to detect.

## Capabilities

### New Capabilities
- `score-editing`: the operations, their semantics, how an edit produces a new
  score, undo and redo, and what remains true of the model after every edit
  (the invariants in `ScoreInvariants` must hold after an edit exactly as they
  do after an import).
- `musicxml-export`: writing a score back to MusicXML — what is written, what is
  preserved from the source document, and what the writer refuses rather than
  silently losing.

### Modified Capabilities
- `normalized-score-model`: a score is still immutable, and that requirement is
  reaffirmed rather than relaxed. What changes is that the model gains a stated
  story for *succession* — how one score replaces another and what a reader may
  assume across that boundary.
- `musicxml-import`: the importer must retain enough of the source document for
  the writer to put back what the model does not hold. Today it discards it.
- `score-import-command`: an open score becomes a document with unsaved changes,
  which means a dirty state, a save command, and a prompt before discarding.

## Impact

- **New**: the edit operations and their application, an undo stack, a MusicXML
  writer, and a source-document retention mechanism in the importer.
- **Changed**: `CurrentScore` gains a publication path from editing as well as
  from import; the shell gains save/undo/redo commands and a dirty indicator.
- **Risk, stated plainly**: this is the largest change proposed and the only one
  that can destroy user data. A bug in the writer silently corrupts the file the
  user was practising from. Writing to a temporary file and moving it into place,
  never writing in place, is a hard requirement rather than a refinement.
- **Not affected**: the audio thread, which may not touch a score at all, and is
  no more involved after this than before.

## Long-term compatibility

Every score file the application has ever written must stay readable by it, and
by other notation software — the format is not ours and compatibility is
measured against MuseScore, Finale, Sibelius and Dorico reading our output, not
against our own importer round-tripping. The corpus test is the shape that check
should take: write every corpus file back out, re-import it, and require the
model to match. Undo history is in-memory only and deliberately does not
persist, so it carries no compatibility obligation.
