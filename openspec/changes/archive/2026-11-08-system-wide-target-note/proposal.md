## Why

The Target Note tool was built to prove the score model could be consumed at
all, and it did that — `docs/development/architecture/score-model-findings.md`
is its real deliverable. As a *tool* it was never the right shape: "which note
am I aiming at" is not something a user should have to open a panel to see, and
every tool that could use it — the tuner first — cannot, because the target
lives inside one tool's private state.

So the target note stops being a tool and becomes **application state**, in the
same way `CurrentScore` already is: published once, read by any tool through
`ToolServices`, and shown by whichever surface is in front of the user.

The second half of this is a scope correction. A target note only means
something for a **monophonic melodic instrument** — singing, saxophone, flute,
any one-note-at-a-time line. For a piano it is incoherent: there is no single
note being aimed at, and the pitch detection that would check it does not exist
(#204, polyphonic detection, is deferred). Saying so in the model is better than
letting the feature quietly misbehave on a piano score.

## What Changes

- **A system-wide current target**, in `src/platform/score/` or
  `src/application/`, holding the note the user is currently aiming at and how
  it was chosen. Read through `ToolServices` like `CurrentScore`, message thread
  only, same `shared_ptr`/pointer-identity arrangement.
- **The tuner consumes it**: alongside live pitch it shows the target, the
  interval to it, and whether the player is on it. This is the feature the
  integration was asked for.
- **A shared reference pitch** that belongs to the application rather than to
  the tuner. Finding 2 of the findings document: the only A440 in the codebase
  is `constexpr double referenceFrequencyHz = 440.0` private to
  `PitchTracker.cpp`, and nothing can compare a target against live audio
  without it. **This change is blocked on that and closes it.**
- **Monophonic-only, stated and enforced.** A target is offered for a part the
  application can treat as monophonic, and a polyphonic part says so plainly
  rather than picking an arbitrary note of a chord. The existing
  `selectTargetNote` already picks arbitrarily from a chord and cannot tell that
  it is — finding 6.
- **BREAKING: the `target-note` tool is removed from the catalog.** Its id
  becomes an alias on whatever tool absorbs it so that a saved workspace naming
  it still resolves rather than failing to open. `ToolCatalog` already supports
  aliases for exactly this.
- `selectTargetNote` and its tests survive the tool's removal — the selection
  logic is the part worth keeping, and it is the only part that was ever tested.

## Capabilities

### New Capabilities
- `target-note-state`: the application-wide current target note — what it is,
  how it is chosen, which parts can have one, and how a tool reads it.
- `application-reference-pitch`: one tuning reference owned by the application,
  settable once, used by every consumer that converts between pitch and
  frequency.

### Modified Capabilities
- `tool-registry`: the catalog loses a tool and gains an alias; the pinned tool
  count changes. The removal path for a tool is currently undescribed and this
  is the first use of it.
- `shared-pitch-analysis`: pitch analysis gains the reference pitch as an input
  rather than a private constant.

## Impact

- **Removed**: `src/features/score/TargetNoteComponent.*`, its catalog entry and
  factory, its approved window state, its surface, and its place in
  `all-tools-docked`.
- **Kept and moved**: `TargetNoteSelection.*` and its tests.
- **New**: the target holder and the reference pitch, both JUCE-free.
- **Changed**: `TunerComponent`, `ToolServices`, `BuiltInToolCatalog.h`,
  `BuiltInTools.cpp`, `ApprovedWindowStates.cpp`, `surfaces.py`, the pinned tool
  count, `PitchTracker.cpp`.
- **Unblocks**: #27–#29, which are all built on pitch matching and all blocked
  on the reference pitch today.
- **Not affected**: the audio thread. The target is message-thread state and the
  reference pitch is a value read, not a score traversal.

## Long-term compatibility

`target-note` must keep resolving as an alias **permanently**, not for one
release: a workspace file written today names it, and
`docs/development/architecture/adding-a-tool.md` already states that a tool id
written into a saved workspace is never changed, only aliased. The reference
pitch becomes a persisted setting, so it needs a `settingsVersion` and a
documented default of A440 for every file written before it existed.
