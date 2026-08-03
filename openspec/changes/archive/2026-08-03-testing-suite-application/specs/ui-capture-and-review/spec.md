## ADDED Requirements

### Requirement: Verification is a standalone application
Verification SHALL be performed by an application separate from Practice Takes —
not a tool inside it, not a panel of it — so that it can verify a build it is not
part of, adds no test-only surface to the shipping binary, and still runs when
the application under test does not.

#### Scenario: The application under test fails to launch
- **WHEN** the build under test cannot start
- **THEN** the testing suite still runs and records the failure, rather than
  failing with it

#### Scenario: A shipped build is inspected
- **WHEN** the shipping binary is examined
- **THEN** it contains no part of the testing suite

### Requirement: Capture and review are separate passes
The suite SHALL separate capture from judgement: one pass produces every image
with no reviewer present, and a later pass presents them for judgement. The
review pass SHALL NOT require the application under test to be running, present,
or buildable.

#### Scenario: Capture runs unattended
- **WHEN** a capture pass is started
- **THEN** it drives the application and produces every image without prompting
  anyone

#### Scenario: The reviewer arrives afterwards
- **WHEN** a reviewer opens the review pass after a capture pass has finished
- **THEN** every image is already available, and the application under test does
  not need to be launched

### Requirement: Capture reuses the existing control and capture mechanisms
The capture pass SHALL drive the application through the existing test-control
channel and its approved window states, and SHALL capture windows through the
existing X capture utilities under `tools/scripts/quality/ui-validation/`. It
SHALL NOT introduce a second mechanism for driving or for capturing.

#### Scenario: A surface is reached
- **WHEN** the capture pass moves to a surface
- **THEN** it names an approved state over the control channel rather than
  synthesising input

#### Scenario: A capture is taken
- **WHEN** an image is produced
- **THEN** it comes from the existing capture utility rather than from a
  separate implementation

### Requirement: Every surface is captured at every configured resolution
The capture pass SHALL capture each surface in the run at each resolution in the
run's configured set, and SHALL record which resolution each image was captured
at. A surface declared as being about its own window size SHALL be captured only
at its own geometry.

#### Scenario: A run with several resolutions
- **WHEN** a capture pass runs with three resolutions configured
- **THEN** each surface produces one image per resolution, each labelled with
  the resolution it used

#### Scenario: A surface that is about its size
- **WHEN** the pass reaches a surface whose whole point is its window size
- **THEN** that surface is captured once, at its own geometry, and is not
  resized through the configured set

### Requirement: The capture pass verifies the geometry it asked for
Before capturing, the pass SHALL confirm that the window actually adopted the
requested geometry, and SHALL record a capture failure rather than an image when
it did not within the allowed settling time.

#### Scenario: The window manager applies the resize
- **WHEN** a resize is requested and the window reaches the requested size
- **THEN** the image is captured and recorded at that resolution

#### Scenario: The window does not reach the requested size
- **WHEN** the window has not adopted the requested geometry within the allowed
  settling time
- **THEN** a capture failure is recorded naming the requested and actual sizes,
  and no image is recorded for that resolution

### Requirement: A surface that cannot be captured is a recorded failure
A surface the pass cannot reach or cannot capture SHALL be recorded as a failure
of that surface, with the reason, and the pass SHALL continue to the next
surface. It SHALL NOT be silently absent from the run.

#### Scenario: A state cannot be opened
- **WHEN** the application refuses or fails to open an approved state
- **THEN** that surface is recorded as failed with the reason, and the pass
  continues

#### Scenario: A failed capture is reviewed
- **WHEN** the reviewer reaches a failed capture in the grid
- **THEN** it appears as a recorded failure with its reason, not as a missing
  tile

### Requirement: Review presents every capture in one grid
The review surface SHALL present all of a run's captures together in one
scrollable grid, grouped by surface, with a surface's resolutions shown beside
one another, so that a layout problem visible only at some sizes is visible by
comparison rather than by memory.

#### Scenario: A run is reviewed
- **WHEN** the reviewer opens a run
- **THEN** every capture is in one grid, grouped by surface

#### Scenario: Comparing sizes
- **WHEN** a surface was captured at several resolutions
- **THEN** those images appear beside one another

### Requirement: An image can be opened at full size
The reviewer SHALL be able to open any image at its captured size from the grid,
and return to the grid without losing scroll position or selection.

#### Scenario: Zooming an image
- **WHEN** the reviewer opens an image from the grid
- **THEN** it is shown at its captured size

#### Scenario: Returning to the grid
- **WHEN** the reviewer closes a zoomed image
- **THEN** the grid is where they left it, with any selection intact

### Requirement: A tag applied to a selection lands on every selected image
The reviewer SHALL be able to select several images and apply a tag to all of
them in one action, and to remove a tag the same way.

#### Scenario: Tagging a multi-select
- **WHEN** the reviewer selects several images and applies a tag
- **THEN** every selected image carries that tag

#### Scenario: Removing a tag from a multi-select
- **WHEN** the reviewer selects several tagged images and removes a tag
- **THEN** it is removed from every selected image that had it, and images
  without it are unaffected

### Requirement: The tag vocabulary is data
The set of tags SHALL be stored data rather than a fixed list in code, SHALL
start with `broken`, `ugly`, and `illegible`, and SHALL be extendable by the
reviewer during a review without changing code or restarting the review.

#### Scenario: A new tag is needed mid-review
- **WHEN** the reviewer adds a tag during a review
- **THEN** it becomes available immediately and can be applied like any other

#### Scenario: Tags persist across runs
- **WHEN** a later run is reviewed
- **THEN** previously added tags are still available

### Requirement: Any image can carry a free-text comment
The reviewer SHALL be able to attach free text to any single image, and that
comment SHALL be kept with the run.

#### Scenario: Commenting on an image
- **WHEN** the reviewer writes a comment on an image
- **THEN** it is stored against that image and that run

#### Scenario: Reading a run's comments later
- **WHEN** a past run is opened
- **THEN** its comments are shown against the images they were written on

### Requirement: The three fixed axes are answered during review
For every surface at every captured resolution, the review SHALL ask the same
three axes — whether it looks correct, whether it looks well-presented, and
whether it functions — each answerable as pass, fail, or skipped, and SHALL
require a note on a failure. Tags and comments SHALL NOT substitute for an axis.

#### Scenario: Scoring a capture
- **WHEN** the reviewer scores a capture
- **THEN** all three axes are answerable independently for it

#### Scenario: A failure without a reason
- **WHEN** the reviewer marks an axis as failed and writes no note
- **THEN** the run cannot be exported until a note is given

#### Scenario: A tagged image with no verdict
- **WHEN** an image carries tags but no axis verdicts
- **THEN** it is reported as unscored rather than counted as passing

### Requirement: A review in progress survives leaving it
Verdicts, tags, and comments SHALL be persisted as they are given, so that
closing the review and returning later resumes with everything already entered,
and an unfinished review is exported as an incomplete run rather than as one in
which the unscored surfaces passed.

#### Scenario: The reviewer stops partway
- **WHEN** the reviewer closes the review with surfaces still unscored
- **THEN** everything entered so far is kept

#### Scenario: An unfinished review is exported
- **WHEN** a run with unscored surfaces is exported
- **THEN** the record is marked incomplete and names what was not scored

### Requirement: Questions a still image cannot answer stay attended
A question about behaviour rather than appearance — whether a tool responds to
live microphone input, whether a control does what it says, whether state
survives a restart — SHALL NOT be answered from a captured image. Surfaces
carrying such questions SHALL be marked as needing an attended pass, and the
suite SHALL run those in a short attended pass against a live application,
recording their answers into the same run as the captures.

#### Scenario: A surface asks about live input
- **WHEN** a surface declares a question about the application responding to
  live audio
- **THEN** that question is asked in the attended pass with the application
  running, not inferred from a still image

#### Scenario: The attended pass has not been run
- **WHEN** a run is exported with attended questions unanswered
- **THEN** those questions are recorded as unanswered and the run is incomplete,
  rather than being reported as passing

#### Scenario: The attended pass is short
- **WHEN** the attended pass runs
- **THEN** it covers only the surfaces that declare behavioural questions, not
  every surface in the run

### Requirement: The suite runs only locally
The testing suite SHALL be a local development tool. No CI check SHALL invoke
its capture or review passes, since both require a real display and the review
requires a person; CI reads only the records a run exports.

#### Scenario: CI runs without a display
- **WHEN** CI runs
- **THEN** no check invokes the capture or review pass

#### Scenario: CI consumes verification
- **WHEN** CI needs to know what was verified
- **THEN** it reads the exported run record
