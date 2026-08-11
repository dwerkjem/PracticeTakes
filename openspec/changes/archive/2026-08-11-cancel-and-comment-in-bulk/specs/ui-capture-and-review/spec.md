## ADDED Requirements

### Requirement: A run in progress can be stopped
The interface that starts a capture run SHALL offer a way to stop it while it is
running, and SHALL accept the Escape key for the same purpose. The run SHALL
stop at the next surface boundary rather than mid-capture, so that no partial
image is recorded.

Captures already taken SHALL be kept. Surfaces not reached SHALL be left
uncaptured rather than recorded as failures: a stopped run and a broken one are
different, and only one of them describes a defect in the build.

A stopped run SHALL be resumable by running it again, which skips what it
already holds.

#### Scenario: Stopping a long sweep
- **WHEN** an operator stops a run that has captured some of its surfaces
- **THEN** the run ends without capturing the rest
- **AND** what it captured is still in the store and reviewable

#### Scenario: A stopped run is not a failed one
- **WHEN** a run is stopped before it finishes
- **THEN** the surfaces it did not reach carry no failure, and the run is not
  reported as having failed

#### Scenario: Continuing afterwards
- **WHEN** a stopped run is started again
- **THEN** the surfaces it already captured are skipped and the rest are taken

#### Scenario: The keyboard
- **WHEN** the operator presses Escape while a run is in progress
- **THEN** the run stops as though the stop control had been used

### Requirement: A comment can be written against several captures at once
The review SHALL let a comment be written once and applied to every capture in
the current selection, using the same narrowing that already governs which
captures are shown and approved together.

Each capture SHALL carry the comment individually afterwards, so that a capture
read on its own still shows what was said about it.

#### Scenario: One defect across several images
- **WHEN** a reviewer narrows to several captures and writes one comment
- **THEN** every capture in that selection carries it

#### Scenario: Reading a capture on its own
- **WHEN** a capture that received a bulk comment is opened by itself
- **THEN** the comment is shown against it like any other

### Requirement: A comment can be removed
The review SHALL let a comment be deleted from the capture it was written
against. A comment is a note somebody typed rather than a record of what the
build did, and typing one against the wrong capture is easy.

Deleting one comment SHALL leave every other comment on that capture untouched,
and asking to delete one that is not there SHALL say so rather than report
success.

#### Scenario: A comment written by accident
- **WHEN** a reviewer deletes a comment
- **THEN** it is gone from that capture, and the capture's other comments remain

#### Scenario: Deleting one that has already gone
- **WHEN** a comment that no longer exists is deleted
- **THEN** the review says so rather than reporting that it worked
