# The score feature roadmap

Five proposed changes, their order, and what each depends on. They came out of
one request and are written down together because the dependencies between them
are the whole reason the order matters — three of the five are blocked on
something else in the list, and two are blocked on findings recorded months
before any of them was proposed.

This is a planning document. Each entry links to the OpenSpec change that
specifies it; the change is the authority and this is the map. Tracked on GitHub
as [#231](https://github.com/dwerkjem/PracticeTakes/issues/231).

## Order

```
engraved-notation-renderer  (in progress, 44/47)
        |
        +-- engraving-stems-and-beams
        |        |
        |        +-- engraving-polish
        |
        +-- score-library  (previews render through the layout engine)

score-model-findings #2  (shared reference pitch)
        |
        +-- system-wide-target-note

musicxml-import  (source-document retention)
        |
        +-- live-score-editing
```

Nothing in the right-hand column can start before the thing to its left is
done, and the two trees are independent of each other.

## The changes

### 1. `engraving-stems-and-beams` — readability ([#226](https://github.com/dwerkjem/PracticeTakes/issues/226))

[`openspec/changes/engraving-stems-and-beams/`](../../../openspec/changes/engraving-stems-and-beams/proposal.md)

Stems, flags, beams, augmentation dots. **Do this first of the five.** The page
currently draws pitch correctly and time not at all: an eighth note and a
quarter note are indistinguishable, so the score is unreadable as rhythm. Every
other visual change is a refinement of a page that has to be readable before
refining it means anything.

Blocked by: `engraved-notation-renderer` finishing. Blocks: `engraving-polish`.

### 2. `system-wide-target-note` — the tuner integration ([#227](https://github.com/dwerkjem/PracticeTakes/issues/227))

[`openspec/changes/system-wide-target-note/`](../../../openspec/changes/system-wide-target-note/proposal.md)

The target note stops being a tool and becomes application state, read by the
tuner. Scoped to **monophonic melodic instruments** — voice, saxophone, flute —
because a target note is incoherent for a piano and the polyphonic detection
that would check one is deferred (#204).

Blocked by: **finding 2** of
[`score-model-findings.md`](score-model-findings.md) — there is no application
reference pitch, only a `440.0` private to `PitchTracker.cpp`, so nothing can
compare a target against live audio. That is the single most consequential item
on the findings list and it blocks #27–#29 as well as this.

Carries a **breaking change**: the `target-note` tool leaves the catalog.

### 3. `score-library` — scores you can find again ([#228](https://github.com/dwerkjem/PracticeTakes/issues/228))

[`openspec/changes/score-library/`](../../../openspec/changes/score-library/proposal.md)

A nominated folder, scanned, listed with metadata, each score previewed as a
rendered image of its first system, cached on disk.

Blocked by: `engraved-notation-renderer`, because a preview is the layout engine
run without a window. Worth noting that this is the first thing to render a
score *without displaying it*, which is the useful shape for the engraving layer
to be proven in.

### 4. `engraving-polish` — publication standard ([#230](https://github.com/dwerkjem/PracticeTakes/issues/230))

[`openspec/changes/engraving-polish/`](../../../openspec/changes/engraving-polish/proposal.md)

Key and time signatures first — not drawn at all today, and a correctness gap
rather than a polish item — then collisions, ties and slurs, dynamics, lyrics,
repeats, justification.

Blocked by: `engraving-stems-and-beams`. The change most likely to sprawl, which
is why its scope is a closed list.

### 5. `live-score-editing` — hot re-render ([#229](https://github.com/dwerkjem/PracticeTakes/issues/229))

[`openspec/changes/live-score-editing/`](../../../openspec/changes/live-score-editing/proposal.md)

Editing a score in the application, re-rendering as it changes, writing back to
the `.mxl`.

The largest of the five and the only one that can destroy user data. Two things
about it are settled in advance and should not be relitigated during
implementation:

- **The score stays immutable.** An edit builds a new `Score` and republishes
  it. `Score.h` explains why `const` is load-bearing: it is the entire
  enforcement of "any number of readers on any number of threads need no lock".
  Copy-on-write keeps that; mutation would delete it and silently put every
  existing reader into a data race.
- **Write-back must preserve what the model does not hold.** The model is a
  normalised subset and the importer drops constructs with diagnostics, so a
  naive writer silently deletes the user's engraving. Never write in place.

## What is not here

- **Polyphonic pitch detection** (#204) — deferred, and the reason the target
  note is scoped to monophonic instruments.
- **Playback** (#35–#38) — none of the five depends on it, and it depends on the
  reference pitch that `system-wide-target-note` introduces.
- **Cross-staff beams** — out of scope in `engraved-notation-renderer` and still
  out of scope in both engraving changes. The diagnostic stays.
- **A score editor proper** — `live-score-editing` is correcting a wrong note
  while practising, not competing with MuseScore.

## Long-term support

Per decision, each proposal carries its own long-term compatibility section
rather than this being a separate policy or a separate branch. The three
commitments worth repeating here, because they outlive any one change:

1. **A tool id written into a saved workspace is never reused or dropped**, only
   aliased. `target-note` must keep resolving permanently.
2. **A score file is read, and written only by `live-score-editing`**, never in
   place, and compatibility is measured against other notation software reading
   our output — not against our own importer round-tripping.
3. **Caches are disposable and versioned; indexes are neither.** The preview
   cache may be deleted at any time; the record of which folders a user
   nominated may not.
