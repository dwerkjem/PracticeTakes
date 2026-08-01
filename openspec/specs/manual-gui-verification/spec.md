## Purpose

Defines the interactive harness that drives the application to each surface, asks a tester about it, records the run, and gates releases on a current full run.

## Requirements

### Requirement: Manual verification is driven by an interactive harness
Manual GUI verification SHALL be performed through a harness that launches the
application, navigates it to each surface under test, and prompts the tester in
a terminal UI, rather than through a document the tester reads and follows by
hand.

#### Scenario: A verification run is started
- **WHEN** the tester starts the harness
- **THEN** the application is launched and brought to the first surface under
  test without the tester navigating to it manually

#### Scenario: The tester is never asked to find something themselves
- **WHEN** the harness prompts about a surface
- **THEN** that surface is already displayed, so the prompt is about what is on
  screen rather than about something the tester must first locate

### Requirement: The harness advances through surfaces automatically
After each surface is answered, the harness SHALL drive the application to the
next surface in the run without tester intervention, and SHALL report a surface
it could not reach as a failure of that surface rather than silently skipping
it.

#### Scenario: Advancing after an answer
- **WHEN** the tester submits their answers for a surface
- **THEN** the application is driven to the next surface and the next prompt is
  shown

#### Scenario: A surface cannot be reached
- **WHEN** the harness cannot navigate to a surface — a control is missing, or
  the application stops responding
- **THEN** that surface is recorded as failed with the reason, and the run
  continues to the next surface

### Requirement: Every surface is scored on three fixed axes
For every surface, the harness SHALL ask the same three questions — whether it
looks correct, whether it looks well-presented, and whether it functions — each
answerable as pass, fail, or skipped, so that results are comparable across
surfaces and across runs.

#### Scenario: Answering the fixed axes
- **WHEN** a surface is presented
- **THEN** the tester is asked all three axes and may answer each independently

#### Scenario: Comparing a surface across runs
- **WHEN** two runs of the same surface are compared
- **THEN** the same three axes are present in both, so a regression in any one
  of them is visible

### Requirement: Surfaces may add their own questions
A surface SHALL be able to declare additional questions specific to it, asked
after the three fixed axes and recorded separately from them, so that
surface-specific checks do not dilute the comparable core.

#### Scenario: A surface with an extra check
- **WHEN** a surface declares an additional question
- **THEN** it is asked after the three fixed axes and recorded as an extra
  rather than as one of the three

### Requirement: Free-text notes can accompany any answer
The harness SHALL allow the tester to attach a free-text note to any answer, and
SHALL require one when an answer is a failure, so that a recorded failure is
actionable without re-running the surface.

#### Scenario: A failure is recorded
- **WHEN** the tester marks any question as failed
- **THEN** a note is required before the run can advance

#### Scenario: A note on a passing answer
- **WHEN** the tester passes a question but wants to record an observation
- **THEN** a note may be attached without changing the verdict

### Requirement: The harness offers a full mode and a quick mode
The harness SHALL provide two run modes: a full mode covering every surface and
every question, intended to be run before a release; and a quick mode covering a
reduced set intended to answer only whether the application still works. The
mode SHALL be recorded with the run.

#### Scenario: A quick run before committing
- **WHEN** the tester runs the harness in quick mode
- **THEN** only the reduced set is presented, and the record identifies the run
  as a quick run

#### Scenario: A full run before a release
- **WHEN** the tester runs the harness in full mode
- **THEN** every surface and question is presented, and the record identifies
  the run as a full run

#### Scenario: A quick run is not mistaken for a release check
- **WHEN** a quick run's record is read
- **THEN** it is distinguishable from a full run without inspecting which items
  it happened to contain

### Requirement: An optional flag repeats surfaces at multiple window sizes
The harness SHALL accept a flag that, when set, presents each surface at several
window geometries — including a constrained size, the default size, and a
maximised window — so that layout problems that only appear at some sizes are
caught. When the flag is not set, each surface is presented once at the default
geometry.

#### Scenario: The scaling flag is set
- **WHEN** the harness runs with the geometry flag enabled
- **THEN** each surface is presented and scored once per configured geometry,
  and each answer records which geometry it applied to

#### Scenario: The scaling flag is not set
- **WHEN** the harness runs without the flag
- **THEN** each surface is presented once and the record states that only the
  default geometry was covered

### Requirement: The harness covers the audio and workspace surfaces
The full run SHALL include, at minimum: microphone device selection and
switching, the global mute and gain controls, each analysis tool opening and
displaying live input, moving a tool between docked, floating, and tabbed
presentation, workspace layout persisting across a restart, and the settings
import and export round trip.

#### Scenario: A full run completes
- **WHEN** a full run finishes
- **THEN** every listed surface has been presented and scored

### Requirement: The harness writes the run record itself
On completion the harness SHALL write a record containing the date, the commit
verified, the platform, the audio device in use, the mode, whether the geometry
flag was set, and every answer with its notes. The tester SHALL NOT have to
transcribe results by hand.

#### Scenario: A run finishes
- **WHEN** the last surface is answered
- **THEN** a complete record is written without further tester action

#### Scenario: The record identifies what was verified
- **WHEN** a record is read later
- **THEN** it names the commit, platform, and audio device, so it is clear what
  the results apply to

### Requirement: An interrupted run preserves what was answered
If a run is interrupted, the harness SHALL preserve the answers already given
and mark the run incomplete, so that a long full run is not lost and an
incomplete run is never mistaken for a passing one.

#### Scenario: The tester interrupts a full run
- **WHEN** a run is interrupted partway
- **THEN** the answers given so far are written and the record is marked
  incomplete

#### Scenario: An incomplete record is read
- **WHEN** an incomplete record is compared against a complete one
- **THEN** it is identifiable as incomplete rather than appearing to be a run in
  which the remaining surfaces passed

### Requirement: Automated coverage removes manual questions
When an automated test is added that covers a question the harness asks, that
question SHALL be removed from the harness in the same change, so the manual run
shrinks as automation grows.

#### Scenario: A smoke test covers a manual question
- **WHEN** an automated test is added that verifies a question the harness asks
- **THEN** that question is removed from the harness in the same change

### Requirement: A release is blocked without a current full manual run
The release workflow SHALL fail before producing or publishing any artifact when
there is no complete, current, full-mode manual run record for the code being
released. This applies to both release entry points: the manual dispatch that
bumps `VERSION`, and a pushed release tag.

#### Scenario: A release is attempted with no record
- **WHEN** a release is triggered and no full-mode run record exists
- **THEN** the release fails before any artifact is built or published

#### Scenario: A release is attempted with only a quick run
- **WHEN** the most recent run record is a quick run
- **THEN** the release fails, because quick mode is not a release check

#### Scenario: A release is attempted with an incomplete run
- **WHEN** the most recent full run record is marked incomplete
- **THEN** the release fails

#### Scenario: A release proceeds
- **WHEN** a complete, current, full-mode record exists for the code being
  released
- **THEN** the gate passes and the release continues

### Requirement: A run record is matched to a release by code state
A manual run verifies the application as built from some commit, and the record
of that run is necessarily committed afterwards — so a record can never name the
commit that contains it. The gate SHALL therefore treat a record as current when
the commit it verified is an ancestor of, or identical to, the commit being
released, **and** no file that affects the built application differs between
those two commits.

#### Scenario: The record was committed after the run
- **WHEN** a run verified commit A and its record was committed as commit B, and
  the release is from commit B
- **THEN** the record is accepted, because committing the record changed no file
  that affects the built application

#### Scenario: Application code changed after the run
- **WHEN** a source file affecting the built application changed between the
  verified commit and the commit being released
- **THEN** the record is stale and the release fails, naming the files that
  changed

#### Scenario: Only documentation changed after the run
- **WHEN** the only changes between the verified commit and the release commit
  are to documentation, tests, or repository tooling
- **THEN** the record remains current and the release proceeds

#### Scenario: The verified commit is not an ancestor
- **WHEN** the record names a commit that is not an ancestor of the commit being
  released
- **THEN** the record is not accepted, because it verified a different line of
  development

### Requirement: The set of release-affecting files is explicit
The gate SHALL work from an explicit, documented list of paths that affect the
built application, so that what does and does not invalidate a manual run is a
reviewable decision rather than an inference.

#### Scenario: A new release-affecting path is added
- **WHEN** a change introduces a directory that affects the built application
- **THEN** it is added to the documented list, and its absence would otherwise
  be a gap the gate cannot detect

### Requirement: Failed items block a release unless explicitly waived
A full run containing a failed item SHALL block the release. A release MAY still
proceed if the record carries an explicit, written waiver for each failed item,
so that a known cosmetic defect can ship as a deliberate, recorded decision
rather than by the gate being bypassed.

#### Scenario: An unwaived failure
- **WHEN** a full run record contains a failed item with no waiver
- **THEN** the release fails and names the failed item

#### Scenario: A waived failure
- **WHEN** every failed item carries a written waiver
- **THEN** the release proceeds, and the waivers remain in the record as part of
  the release's history

### Requirement: A release may explicitly skip manual verification
The release workflow SHALL provide a flag that skips the manual verification
gate, defaulting to not skipping, so that a change too small to warrant a full
run is not forced through one.

#### Scenario: A small release skips the gate
- **WHEN** a release is triggered with the skip flag set and a reason given
- **THEN** the gate does not block and the release proceeds

#### Scenario: The flag is not set
- **WHEN** a release is triggered without the skip flag
- **THEN** the gate applies normally

### Requirement: Skipping requires a stated reason
Setting the skip flag SHALL require a written reason, and a skip request without
one SHALL fail, so that skipping is a deliberate decision rather than a default
that drifts into always being set.

#### Scenario: Skip requested with no reason
- **WHEN** the skip flag is set but no reason is supplied
- **THEN** the release fails and asks for a reason

### Requirement: A skipped release is identifiable afterwards
When the gate is skipped, the release SHALL record that manual verification was
skipped and why, in a place that survives with the release, so that which
releases shipped without manual verification is answerable later without
inspecting workflow logs.

#### Scenario: Auditing past releases
- **WHEN** past releases are reviewed
- **THEN** those that skipped manual verification are identifiable along with
  the reason each gave

#### Scenario: A skipped release is not mistaken for a verified one
- **WHEN** a skipped release's record is compared against a verified one
- **THEN** it is distinguishable without inspecting workflow logs

### Requirement: The gate explains why it blocked
When the gate fails it SHALL state which condition failed — missing, quick-only,
incomplete, stale, or unwaived failure — and, for staleness, which
release-affecting files changed since the verified commit.

#### Scenario: A stale record blocks a release
- **WHEN** the gate fails because the record is stale
- **THEN** the failure message names the verified commit and the
  release-affecting files that changed since it

### Requirement: The harness is not required to run in CI
The harness SHALL be a local development tool. No CI check SHALL invoke it,
since it requires a real display, a real audio device, and a human. CI reads the
records it produces; it never runs it.

#### Scenario: CI runs without a display or a tester
- **WHEN** CI runs
- **THEN** no check invokes the manual harness

#### Scenario: An ordinary pull request
- **WHEN** a pull request that is not a release runs CI
- **THEN** no check fails for the absence of a manual run record
