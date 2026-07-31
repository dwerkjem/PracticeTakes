## ADDED Requirements

### Requirement: A manual GUI checklist exists and is versioned
The repository SHALL contain a manual GUI verification checklist under
`docs/development/`, tracked in git so that changes to what is verified are
reviewable in the same way as code.

#### Scenario: The checklist is changed
- **WHEN** a step is added to or removed from the manual checklist
- **THEN** the change appears in the repository's history and is reviewable in a
  pull request

### Requirement: The checklist covers what automation cannot
Each checklist item SHALL state what is being verified and why it cannot be
covered by an automated test, so that the checklist shrinks as automation grows
rather than accumulating items indefinitely.

#### Scenario: An item becomes automatable
- **WHEN** an automated test is added that covers a checklist item
- **THEN** that item is removed from the manual checklist in the same change

#### Scenario: An item is added without justification
- **WHEN** an item is proposed that an existing automated test already covers
- **THEN** it is rejected in review as redundant

### Requirement: The checklist covers the audio and workspace surfaces
The checklist SHALL include, at minimum, verification of: microphone device
selection and switching, the global mute and gain controls, each analysis tool
opening and displaying live input, moving a tool between docked, floating, and
tabbed presentation, workspace layout persisting across a restart, and the
settings import and export round trip.

#### Scenario: A release is prepared
- **WHEN** the checklist is run
- **THEN** every listed surface has been exercised against a real audio device
  and a real display

### Requirement: The checklist states its cadence and scope
The checklist SHALL state when it is required to be run — at minimum before a
release — and SHALL identify which items are required for every run and which
apply only when a related area changed.

#### Scenario: A change touches only the feedback service
- **WHEN** a change modifies nothing in the audio or workspace paths
- **THEN** the checklist identifies which subset, if any, still applies

### Requirement: Results are recorded
Running the checklist SHALL produce a dated record naming the version or commit
verified, the platform and audio device used, and the outcome of each item,
stored in the repository.

#### Scenario: A checklist run finds a defect
- **WHEN** an item fails during a run
- **THEN** the record captures which item failed and on which platform and
  device, so the failure is actionable without re-running the whole checklist

#### Scenario: A clean run before release
- **WHEN** every item passes
- **THEN** a dated record naming the commit, platform, and device is committed
