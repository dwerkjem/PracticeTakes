## Purpose

Defines the two-pass verification workflow: an unattended capture of every surface at every configured resolution, and an attended review over a grid with zoom, multi-select tagging, per-image comments, and the three fixed axes.

## Requirements

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

#### Scenario: The pointer during a capture
- **WHEN** a capture is taken for review
- **THEN** the pointer is left where it is, because a window capture does not
  include the cursor and only pixel-for-pixel comparison is affected by hover
  state

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

### Requirement: Every surface is captured in every configured palette
A run SHALL cover a configured set of palettes, capturing each surface in each
one, and SHALL record which palette each image was captured in. The palette
SHALL be applied after the surface's state is established, so that everything
the state created is drawn in it.

#### Scenario: A run covering two palettes
- **WHEN** a run is captured with the light and dark palettes configured
- **THEN** each surface produces one image per palette, each labelled with the
  palette it used

#### Scenario: A state that rebuilds the workspace
- **WHEN** a surface's state opens tools and the palette is then applied
- **THEN** the tools it created are drawn in that palette rather than the
  previous one

### Requirement: A capture can be judged at the size it is judgeable
Opening a capture SHALL present it large enough to judge, and SHALL allow it to
be approved or rejected from there without returning to the grid, moving on to
the next capture in the current selection.

#### Scenario: Judging from the enlarged view
- **WHEN** the reviewer approves an enlarged capture
- **THEN** every question answerable from that image is recorded as passed and
  the next capture in the filtered set is shown

#### Scenario: Moving without judging
- **WHEN** the reviewer steps forward or back from an enlarged capture
- **THEN** the neighbouring capture in the filtered set is shown and no verdict
  is recorded

### Requirement: The application can be opened on the surface under review
The review SHALL offer, for any capture, to open the application in that
capture's state, palette, and window size, so that a question a still image
cannot settle can be answered by the application itself. Only one such instance
SHALL be open at a time, and it SHALL NOT be opened while a capture pass is
running.

#### Scenario: Opening a surface from its capture
- **WHEN** the reviewer opens the application from a capture
- **THEN** it starts in that capture's state, palette, and window size

#### Scenario: Opening another surface
- **WHEN** the reviewer opens a second surface
- **THEN** the first instance is closed rather than left running alongside it

#### Scenario: A capture pass is running
- **WHEN** the reviewer tries to open a surface while a capture is in progress
- **THEN** it is refused with the reason, and the capture is unaffected

### Requirement: A review can be narrowed to a subset and approved together
The review SHALL let a reviewer narrow the grid by the properties of a capture —
at minimum its palette, its resolution, the part of the application it belongs
to, how its tools are presented, which tools are open, whether it has been
reviewed, how it was judged, and what it is tagged with — offering only the
values the run actually contains. Several filters SHALL combine: alternatives
within one property, and all of them together across properties.

A value SHALL be usable to **exclude** as well as to keep, so that a reviewer can
ask for everything except a group they have already dealt with. Where a value is
both kept and excluded, it SHALL be excluded.

A capture's judged state SHALL report the worst verdict given to it: a failure
anywhere makes it failed, and a skip outranks a pass.

The reviewer SHALL be able to approve everything currently shown in one action,
and the review SHALL state how many captures are shown and how many are hidden,
so that an approval never silently covers captures nobody looked at.

#### Scenario: Narrowing to a question
- **WHEN** the reviewer filters to one palette and one area
- **THEN** only captures matching both are shown, and the count of hidden
  captures is stated

#### Scenario: Two values of one property
- **WHEN** the reviewer chooses two palettes
- **THEN** captures in either are shown, rather than none

#### Scenario: Excluding what has been dealt with
- **WHEN** the reviewer excludes the reviewed captures
- **THEN** every capture that is not fully reviewed is shown, and the reviewed
  ones are hidden

#### Scenario: A capture with one failing answer
- **WHEN** a capture has passes and one failure
- **THEN** it is reported as failed, so a filter for failures finds it

#### Scenario: Approving a filtered set
- **WHEN** the reviewer approves everything shown
- **THEN** every image-answerable question on those captures is answered pass,
  captures hidden by the filters are untouched, and questions already answered
  are left as they were

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

### Requirement: A capture run can be narrowed to named surfaces
The capture pass SHALL accept a set of surfaces to cover, named by their
approved state, and SHALL capture only those. A name that no surface in the
run's mode offers SHALL be rejected before the run starts, naming what was
asked for and what is available. With no set given, the run SHALL cover every
surface its mode covers, as it does today.

Narrowing SHALL compose with the existing resolution and palette sets rather
than replacing them: a narrowed run still visits each chosen surface at every
configured resolution and in every configured palette.

#### Scenario: Capturing one surface
- **WHEN** a capture pass is asked for a single surface by name
- **THEN** only that surface is captured, at every configured resolution and
  palette, and no other surface is visited

#### Scenario: A misspelled surface name
- **WHEN** a capture pass is asked for a name no surface offers
- **THEN** the run fails before capturing anything, reporting the unknown name
  and the names that exist
- **AND** it does not silently complete having captured nothing

#### Scenario: No selection given
- **WHEN** a capture pass is run without naming any surface
- **THEN** it covers every surface its mode covers

### Requirement: A capture run can use a display of its own
The capture pass SHALL be able to run on a private display, so that no window
appears on the operator's screen and nothing takes focus while it runs. The
images produced SHALL be equivalent to those captured on the desktop display.

The private display SHALL be at least as large as the largest geometry the run
captures, so that a geometry defined in terms of the available screen is not
quietly reduced to a smaller one.

When a private display is requested and the mechanism providing it is not
installed, the run SHALL refuse with the command that installs it, and SHALL NOT
fall back to the desktop display — a fallback would put windows on the screen of
someone who asked for a private display precisely so that would not happen.

#### Scenario: Capturing while the machine is in use
- **WHEN** a capture pass runs on a private display
- **THEN** no application window appears on the operator's screen, focus is
  never taken, and every surface is captured as usual

#### Scenario: The display mechanism is absent
- **WHEN** a private display is requested on a machine without it installed
- **THEN** the run refuses, names the command that installs it, and captures
  nothing on the desktop display

#### Scenario: A geometry defined by the screen
- **WHEN** a run on a private display captures a surface at a geometry that
  asks for the whole available screen
- **THEN** the screen is large enough that the geometry differs from the
  ordinary window size

### Requirement: A capture run can be thrown away
The capture pass SHALL be able to write to a temporary store instead of the
verification history, for a capture taken only to be looked at once. Such a run
SHALL NOT appear in the history, SHALL NOT consume a run number in it, and SHALL
report where its images were written.

The images SHALL outlive the process that made them, since looking at them is
the reason the run happened; cleaning them up is left to the machine's temporary
directory rather than done on exit.

Asking for a throwaway run and naming a store, or asking for a throwaway run and
resuming an existing one, SHALL be refused rather than resolved silently: both
choose where the run lives, and a throwaway store has nothing to resume.

#### Scenario: A capture taken to answer one question
- **WHEN** a capture pass runs as a throwaway
- **THEN** its images are written somewhere temporary and reported
- **AND** the verification history contains no new run

#### Scenario: A throwaway run and a named store
- **WHEN** a throwaway run also names a store to write to
- **THEN** it is refused, because both choose where the run lives

#### Scenario: Resuming a throwaway run
- **WHEN** a throwaway run is asked to resume an existing run
- **THEN** it is refused, because a throwaway store is empty by construction

### Requirement: A capture run reports where its images were written
On completion the capture pass SHALL report the directory holding the images it
wrote, alongside the count captured and the way to open the review grid. Someone
who captured a small selection to inspect a change needs the files themselves,
not only a browser view of them.

#### Scenario: A completed run
- **WHEN** a capture pass finishes
- **THEN** the path to its images is printed with the summary

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
