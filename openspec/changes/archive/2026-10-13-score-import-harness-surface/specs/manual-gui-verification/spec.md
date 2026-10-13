## MODIFIED Requirements

### Requirement: The harness covers the audio and workspace surfaces
A full run SHALL include, at minimum: microphone device selection and switching,
the global mute and gain controls, each analysis tool opening and displaying
live input, moving a tool between docked, floating, and tabbed presentation,
workspace layout persisting across a restart, the settings import and export
round trip, and the score import summary window in both its clean and its
diagnostic form. Those of these that are questions about behaviour rather than
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

#### Scenario: The summary window reports diagnostics
- **WHEN** a full run captures the score import summary window on a score whose
  import produced diagnostics
- **THEN** the capture shows each diagnostic naming the element it is about, so
  a group of diagnostics sharing one message is still legible as distinct rows

## ADDED Requirements

### Requirement: The summary window is verified without the file chooser
The surfaces covering the score import summary window SHALL be reached through
an approved state rather than by driving the file chooser, which is a native
dialog outside the harness's reach. The properties that genuinely require the
chooser SHALL remain a documented manual procedure rather than being claimed as
covered.

#### Scenario: The window is captured
- **WHEN** the capture pass reaches a score import surface
- **THEN** it names an approved state that imports a committed fixture, and no
  file dialog is involved

#### Scenario: What the harness does not cover
- **WHEN** a release asks what verifies opening a score a user chose
- **THEN** the answer is the documented manual procedure, because the chooser is
  not in the harness
