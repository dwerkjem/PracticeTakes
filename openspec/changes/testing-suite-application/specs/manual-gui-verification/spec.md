## REMOVED Requirements

### Requirement: Manual verification is driven by an interactive harness
**Reason**: Verification no longer happens in a terminal while the application
is driven live in front of the tester. Capture is now unattended and review is a
separate later pass over a grid of images, which is what makes side-by-side
comparison across window sizes possible at all.

**Migration**: Replaced by `ui-capture-and-review` — "Verification is a
standalone application" and "Capture and review are separate passes". The
guarantee that the tester is never asked to find something themselves is
preserved: they are shown images of surfaces already driven to, rather than a
live window already driven to.

### Requirement: The harness advances through surfaces automatically
**Reason**: There is no longer an answer-then-advance loop to advance. The
capture pass walks every surface with nobody present, and review is
free-navigation over the result rather than a sequence.

**Migration**: Replaced by `ui-capture-and-review` — "Every surface is captured
at every configured resolution" and "A surface that cannot be captured is a
recorded failure". The rule that an unreachable surface is a recorded failure
rather than a silent skip is carried over unchanged.

## MODIFIED Requirements

### Requirement: An optional flag repeats surfaces at multiple window sizes
A run SHALL cover a configured set of window resolutions — including a
constrained size, the default size, and a maximised window — capturing and
scoring each surface once per resolution, so that layout problems that only
appear at some sizes are caught. The set SHALL be configuration rather than a
fixed list in code, and the resolutions a run covered SHALL be recorded with it.
A run MAY be restricted to the default resolution alone, and its record SHALL
then state that only the default geometry was covered.

#### Scenario: A run covers several resolutions
- **WHEN** a run is captured with several resolutions configured
- **THEN** each surface is captured and scored once per resolution, and each
  answer records which resolution it applied to

#### Scenario: A run covers the default size only
- **WHEN** a run is restricted to the default resolution
- **THEN** each surface is captured once and the record states that only the
  default geometry was covered

#### Scenario: The resolution set changes between runs
- **WHEN** two runs covering different resolution sets are compared
- **THEN** each record states which resolutions it covered, so the difference is
  visible rather than inferred from which answers happen to exist

### Requirement: The harness writes the run record itself
On completion the suite SHALL export a record containing the date, the commit
verified, the platform, the audio device in use, the mode, which resolutions
were covered, and every answer with its notes — generated from the stored run
rather than transcribed by hand. Tags and comments SHALL be exported alongside
the answers as additional detail.

#### Scenario: A run finishes
- **WHEN** the last surface is scored
- **THEN** a complete record is exported without further tester action

#### Scenario: The record identifies what was verified
- **WHEN** a record is read later
- **THEN** it names the commit, platform, and audio device, so it is clear what
  the results apply to

#### Scenario: A reviewer's tags and comments are kept
- **WHEN** a run whose images were tagged and commented is exported
- **THEN** the tags and comments appear in the record alongside the answers they
  relate to

### Requirement: An interrupted run preserves what was answered
If a capture pass or a review is interrupted, the suite SHALL preserve what was
already captured, scored, tagged, and commented, and SHALL mark the run
incomplete, so that a long run is not lost and an incomplete run is never
mistaken for a passing one.

#### Scenario: A capture pass is interrupted
- **WHEN** an unattended capture pass is interrupted partway
- **THEN** the captures already produced are kept and the run is resumable
  without recapturing them

#### Scenario: The reviewer interrupts a review
- **WHEN** a review is closed partway through scoring
- **THEN** the answers given so far are kept and the run is marked incomplete

#### Scenario: An incomplete record is read
- **WHEN** an incomplete record is compared against a complete one
- **THEN** it is identifiable as incomplete rather than appearing to be a run in
  which the remaining surfaces passed

### Requirement: The harness covers the audio and workspace surfaces
A full run SHALL include, at minimum: microphone device selection and switching,
the global mute and gain controls, each analysis tool opening and displaying
live input, moving a tool between docked, floating, and tabbed presentation,
workspace layout persisting across a restart, and the settings import and export
round trip. Those of these that are questions about behaviour rather than
appearance SHALL be covered by the attended pass, and a full run SHALL NOT be
complete until that pass has answered them.

#### Scenario: A full run completes
- **WHEN** a full run finishes
- **THEN** every listed surface has been captured, scored, and — where it asks
  about behaviour — answered in the attended pass

#### Scenario: Only the captures were reviewed
- **WHEN** a full run's captures are all scored but its attended questions are
  unanswered
- **THEN** the run is incomplete and the release gate treats it as such

### Requirement: The harness is not required to run in CI
The suite SHALL be a local development tool. No CI check SHALL invoke its
capture, attended, or review passes, since they require a real display, a real
audio device, and a human. CI reads the records it exports; it never runs it.

#### Scenario: CI runs without a display or a tester
- **WHEN** CI runs
- **THEN** no check invokes any pass of the testing suite

#### Scenario: An ordinary pull request
- **WHEN** a pull request that is not a release runs CI
- **THEN** no check fails for the absence of a manual run record
