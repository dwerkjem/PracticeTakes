## Context

Two changes built the halves this one joins.

`musicxml-import` built the model and decided its ownership and threading, and
those decisions bind here rather than being revisited:

- **A score is `std::shared_ptr<const Score>`** — fully built before its first
  share, never mutated after. `const` is what makes any number of readers on any
  number of threads safe without a lock.
- **The audio thread never touches a `Score`.** Not even to read it: a
  `shared_ptr` copy is a contended atomic refcount write, and the tree is
  pointer-chasing over heap nodes. Nothing here goes near the audio thread, and
  nothing here should make it look cheap to.
- **The model holds nothing positional** (invariant 10). No coordinates, fonts,
  stems, or beams. A consumer computes its own layout.

`score-import-command` built the shell side and stopped at the shell:

- `ScoreImportState` holds the current score and exists to enforce one rule
  worth a test: **a failed import must not close the score a user already has
  open.**
- `MainComponent::scoreImport` is declared before `liveTools` specifically so a
  tool can borrow it, with a comment saying nothing borrows it yet.
- That change's own open question — "does the current score belong on
  `MainComponent` at all, or on a small owner of its own?" — was answered
  "partly", and left for the next change in the area. This is that change.

What the tool contract is today:

```cpp
struct ToolServices
{
    AudioInputService& audio;
    SharedPitchAnalysis& pitchAnalysis;
    Theme initialTheme = Theme::light;
};
```

Its own comment says adding a service later "is one field here instead of a
signature change at every factory". This change is the first test of that claim.

Two precedents to copy rather than reinvent:

| Need | Precedent |
|---|---|
| A shared service a tool reads on its own timer | `SharedPitchAnalysis::latestResult()` — consumers poll rather than being pushed an update, because no tool needs to react faster than its own refresh rate already samples |
| Pure logic split out of a `Component` so it is testable without a display | `WorkspaceLayoutState`, and `ScoreImportSummary` in the previous change |

## Goals / Non-Goals

**Goals**

- The current score is reachable from a tool, through the tool contract.
- One target note derived from a real imported score is drawn in the running
  application.
- Adding this tool costs **one catalog entry and one factory** and no shell
  change. If it costs more, the tool contract is wrong, and saying so is worth
  more than the tool.
- The parts worth asserting — which note is the target, how it is described,
  what happens with no score — are unit tested without a display.
- A written account of what the model got wrong, specific enough to become
  issues.

**Non-Goals**

- Engraving or any notation drawing (#32).
- Choosing which note is the target, or moving between notes (#33).
- Comparing the target to live audio (#27–#29), for the reason recorded under
  *Decisions* below.
- Playback or transport (#35–#38).
- Persisting tool settings, or a harness capture surface for the new tool.
- Changing `Score`, its invariants, or the importer.

## Decisions

### The seam: a holder in `ToolServices`, polled rather than pushed

`ToolServices` gains `const score::CurrentScore& currentScore`.

Three alternatives were considered and rejected:

- **Pass `std::shared_ptr<const Score>` by value in `ToolServices`.** Dead on
  arrival: a tool opened before the first import would hold null forever, and a
  tool open across a second import would keep showing the first score. The
  bundle is built once per tool construction (`MainComponentWorkspacePresentation.cpp`),
  not per frame.
- **Push changes to tools through a listener interface.** More machinery, and
  against the precedent: `shared-pitch-analysis` deliberately chose polling
  because no tool needs sub-refresh-rate latency. A newly opened score is the
  least urgent update in the application.
- **Pass `const ScoreImportState&`.** That is a shell UI type under
  `shell/ui/score/`, carrying import summaries and failure text. Putting it in
  the tool contract would hand every tool the shell's import presentation, which
  is exactly the reach-into-the-shell the layering rule forbids.

`CurrentScore` is therefore a small JUCE-free holder in `src/platform/score/`,
beside the model it holds — the same placement as `SharedPitchAnalysis` in
`src/platform/audio/`, and for the same reason: it is shared infrastructure, not
application presentation.

**Change detection by pointer identity, with no generation counter.** A tool
keeps the `shared_ptr` it last rendered and compares it against what the holder
returns now. That is sound rather than merely cheap: while the tool holds its
copy, the old `Score` cannot be freed, so its address cannot be reused by the
new one. Two distinct scores are therefore always two distinct pointers. A
counter would add state to the holder to answer a question a pointer compare
already answers exactly.

**Message thread only, and documented as such.** `publish` and `score()` are
both called on the message thread — import already hands its result back there
before `ScoreImportState::apply` runs. A `shared_ptr` member is not safe to read
and write concurrently, and the honest fix is the constraint, not a mutex: a
lock here would be dead weight that implies a cross-thread use the design
forbids. A tool reading the holder off the message thread is a bug, and the
header says so.

### Who owns the holder

`ScoreImportState` owns the `CurrentScore` and exposes it by reference.

The thing that *writes* a value is the natural owner of it, and
`ScoreImportState` already is that writer — it is where "a failed import must
not close the open score" lives. Making it own the holder keeps one source of
truth; giving `MainComponent` a second `CurrentScore` member next to
`ScoreImportState` would mean two places that both believe they know the current
score, which is the bug this arrangement exists to prevent.

`ScoreImportState::score()` stays, delegating to the holder, so its existing
tests keep asserting the rule they were written for.

Declaration order needs nothing new: `scoreImport` is already before
`liveTools`, so reverse-order destruction tears every tool down before the
holder it borrows. That ordering is the whole of the enforcement — there is no
runtime check — so the comment on it gets updated from "nothing borrows it yet"
to naming what now does.

When #39's session file takes the current score over, the holder moves to the
session and `ScoreImportState` takes a reference to it instead of owning one.
That is a one-line change at that point, which is the reason for introducing the
type now rather than passing the raw `shared_ptr` around.

### Which note is the target

**The first pitched event, in source order, of the first part — lowest voice
index first, grace notes skipped.** Rests are not targets. A chord is one event
carrying several notes, so its first note is the target and the chord is
reported as what it is.

Deliberately not configurable, and deliberately dumb. Choosing a target is #33's
question and depends on a cursor, a selected part, and a transport that none of
which exist. A rule stated in one sentence is a rule a test can pin, and it is
enough to prove the accessors work.

**The sounding pitch, not the written one.** `Note` stores both and says which
is which: `written` is what the renderer draws, `sounding` is what "playback and
any pitch-matching against live audio must use". A target note exists to be
matched against audio, so it is the sounding pitch — and when the two differ
(a transposing part) the tool says both, because a target that silently showed
the written pitch of a clarinet part would be consistently a tone wrong in a way
nothing crashes on.

**Grace notes are skipped.** A grace note is a zero-duration event taking its
time from performance rather than from the bar's arithmetic. A target note with
zero duration is not a useful first thing to show, and the model's own comment
says playback ignores them for now.

### What the tool draws

A text panel, laid out by the tool, showing:

- the score's title (work title, movement title, or the part name as a last
  resort — a file that names neither still has to be identifiable);
- the target note's **spelled name**, large: the one thing the whole change
  exists to put on screen;
- its sounding MIDI number, and its written spelling when the part transposes;
- which part, staff, and voice it belongs to;
- the **printed** bar number it sits in — the model keeps measure numbers as
  strings because a pickup is "0" and a split bar is "12a", and the point of
  keeping that is to be able to show it;
- where it starts and how long it lasts, in seconds, through the score's own
  `TempoMap` — the one pair of numbers that proves the tempo map was read rather
  than defaulted.

No staff, no glyph, no accidental drawn in a font. Invariant 10 means the model
cannot supply a staff position, and computing one is #32's job.

**Not drawn: a comparison against live audio.** The tool is constructed with
`pitchAnalysis` like every other tool, and showing cents deviation from the
target would be a handful of lines. It is left out on purpose: a sounding MIDI
number becomes a frequency only through a reference pitch, and the only
reference pitch in this codebase is `constexpr double referenceFrequencyHz =
440.0` inside `src/features/analysis/tuner/PitchTracker.cpp` — private to
another tool, which a `src/features/*` tool may not reach into. Duplicating the
constant here would put two reference pitches in one application and guarantee
they disagree the first time one becomes configurable. The finding is worth more
than the readout, and #27 is where it gets fixed.

### Spelled names belong to the model

The model stores `step`, `alter`, `octave` and offers no way to render them.
`noteNameForMidi` exists but lives in `src/features/analysis/tuner/PitchTracker.h`
and is MIDI-only: from a MIDI number it cannot know whether the file wrote F♯ or
G♭, and the model keeps the spelling precisely so that distinction survives.

So `score::spelledName(const Pitch&)` goes in a new
`src/platform/score/PitchNames.h`, not in `Pitch.h`: `Pitch.h` is
`constexpr`-only and includes no `<string>`, and keeping it that way is worth
more than putting everything pitch-shaped in one file.

Double accidentals render as repeated signs ("B♭♭"), and anything beyond a
double is rendered rather than clamped — the importer already rounds microtones
to a semitone with a diagnostic, so an extreme alter means the file really said
so and hiding it would be worse than showing it. ASCII `#` and `b` rather than
♯ and ♭: the tuner already shows `C#4`, and one application should spell a note
one way.

### Testability

The split follows `ScoreImportSummary` exactly. A JUCE-free
`TargetNoteSelection` turns a `std::shared_ptr<const Score>` into a described
target — or into "no score" / "no pitched note" — and that is where every rule
worth asserting lives: which event is chosen, rests and grace notes skipped,
voice order, sounding versus written, the printed bar number, the seconds from
the tempo map, the title fallback.

`TargetNoteComponent` is `Component` code: a timer, a paint routine, and a theme.
It stays outside `PracticeTakesTests` like every other `Component` in the
repository, and the manual GUI harness is where it is seen.

Tests run against **real corpus files** from `src/tests/resources/musicxml/`,
not only hand-built scores. A hand-built `Score` proves the selection rule; a
real file proves the accessors work on what the importer actually produces,
which is the whole point of the spike.

## Risks / Trade-offs

- **A new tool in the roster for a spike.** Mitigated by it being genuinely
  useful on its own — "what note does this file start on, and when" is a real
  question — and by the catalog being the cheapest possible place to put it. If
  #33's score view supersedes it, retiring it is one entry out of each of two
  lists, which is the cost the tool contract was built to make small.
- **The target rule will look naive next to #33.** Accepted. It is one sentence,
  it is tested, and replacing it is a function body.
- **`ToolServices` grows.** It is now three services and a theme. The struct was
  chosen for this, but there is a size past which "the bundle of everything"
  stops being a contract. Recorded as a question rather than pre-solved.
- **The holder is message-thread-only by documentation, not by construction.**
  A future consumer reading it off a worker thread would be a data race no test
  would catch. The alternative — a mutex on every read — buys safety against a
  use the design already forbids and makes the forbidden use look supported.
  Noted in the header where someone would be about to make that mistake.
- **`ScoreImportState` grows a second job.** It now holds both the summary and
  the holder. Sized correctly for today; #39 is where it loses the holder again.

## Migration Plan

Additive throughout. No persisted format, no settings key, no wire format, and
no change to anything the importer produces, so there is nothing to migrate and
no way for an existing session to be affected.

The order that keeps the tree building at every step:

1. `CurrentScore` and `spelledName` with their tests — JUCE-free, no consumer,
   nothing else compiles against them yet.
2. `TargetNoteSelection` with its tests — depends on the model only, still no
   `Component` anywhere.
3. `ScoreImportState` publishes into a holder it owns; existing tests must pass
   unchanged.
4. `ToolServices` gains the field, and both construction sites pass it. Every
   existing factory keeps its signature.
5. `TargetNoteComponent`, the catalog entry, and the factory.
6. The findings document, and the agent guide's stale claim.

Rollback is removing the catalog entry: the services and the holder are inert
without a reader.

## Open Questions

- **How big may `ToolServices` get?** Three services and a theme is fine. Ten
  is a god object handed to every tool by definition. The question is whether
  the next service after this one should go in the bundle or whether the bundle
  should become something a tool asks for a piece of.
- **Does a tool that knows a target note want the same base class as one that
  does not?** #187 asks this and this change can only answer it for one tool.
  The honest answer may be that it only becomes visible with a transport.
- **Should the tool offer the next note, or any note at all?** Deliberately no
  navigation here, but "first note of the file" stops being interesting the
  moment anyone uses it twice. #33 is the owner; the question is whether it
  arrives sooner as a few entries on this tool's options menu.
- **Where does the reference pitch live?** Needed by the tuner (has one,
  private), by any target-matching tool (needs the same one), and by #29's
  reference tempo work. Not answered here beyond recording that it must be
  answered before #27.

## Answers, recorded after implementation (task 7.5)

- **How big may `ToolServices` get?** No clearer than before, but the claim it
  rests on is now tested rather than asserted: adding the third service cost one
  field, one line at each of two construction sites, and no change to any
  existing factory. Three services and a theme is still fine. The question
  stands for the fourth.
- **Does a tool that knows a target note want the same base class as one that
  does not?** **Yes, and the question turned out to be the wrong one.**
  `ToolComponent` fitted without strain — `setTheme`, `resetToDefaults`, no
  settings — and nothing about reading a score rather than audio asked anything
  of it. What *is* different is that this tool has an empty state that most
  sessions see, and `resetToDefaults` means almost nothing for it. Neither needs
  a base class. The real division, when it comes, will be a transport, and this
  change cannot see it.
- **Should the tool offer the next note, or any note at all?** Still no, and the
  screenshot says why it will be asked again immediately: once the first note is
  on screen, "and then what" is the only question left. #33 owns it.
- **Where does the reference pitch live?** Unanswered, and promoted: it is
  finding 2 in the findings document and the one entry there that blocks other
  issues (#27–#29) rather than merely costing a consumer some lines.

Three things the implementation settled that this design did not ask:

- **The target rule as designed was wrong.** "Lowest voice index first" reads
  fine and misreports any measure whose later-numbered voice enters earlier —
  a piano bass under a resting treble, which is ordinary rather than exotic. The
  rule is now the earliest onset across every voice, tie-broken by the lower
  voice number, and there is a test named for the case.
- **A capture surface was in the Non-Goals and should not have been.** Three
  tests fail when a tool has no approved state, and the tool's paint routine has
  no other verification. It has one for its empty state. Its populated state
  cannot be photographed at all, for a reason that is itself a finding: the only
  way the harness can get a score into the application also opens the import
  summary window over whatever the surface is about.
- **The documented cost of adding a tool was understated**, by four edits. Found
  by three tests failing after the tool already worked, which is the wrong order
  to learn it in; `adding-a-tool.md` now says all of it.
