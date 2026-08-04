## ADDED Requirements

### Requirement: A durable store holds everything one run produced
The testing suite SHALL keep a durable store in which one verification run holds
the machine it ran on, the commit and build configuration under test, every
captured image with its verdicts, tags, and comments, every performance
measurement, and every automated test result — so that what a build was like is
answerable from one place rather than by collecting files by hand.

#### Scenario: A run's evidence is read back
- **WHEN** a completed run is opened in the store
- **THEN** its captures, verdicts, tags, comments, measurements, and test
  results are all reachable from that run

#### Scenario: Evidence arrives at different times
- **WHEN** captures are written by an unattended pass and verdicts, tags, and
  comments are added during a later review
- **THEN** both attach to the same run rather than producing two records of it

### Requirement: Every run records the machine it ran on
The store SHALL record, for each run, the hardware and operating system it ran
on — at minimum processor model, core count, total memory, graphics renderer,
operating system, and display resolution — and SHALL attach the run to that
machine, so that a number is never read without knowing what produced it.

#### Scenario: A run is recorded
- **WHEN** a run starts
- **THEN** the machine's hardware and operating system are captured and the run
  is attached to that machine

#### Scenario: A measurement is read later
- **WHEN** a stored measurement is examined
- **THEN** the machine that produced it is identifiable from the store without
  consulting anything outside it

### Requirement: Machine identity ignores volatile system detail
Machine identity SHALL be derived only from facts that do not change under
routine system maintenance. Kernel version, distribution version, and driver
versions SHALL be recorded as attributes of a run but SHALL NOT contribute to
machine identity, so that an ordinary upgrade annotates a machine's timeline
rather than silently starting a new one.

#### Scenario: The kernel is upgraded between runs
- **WHEN** two runs are recorded on the same hardware either side of a kernel
  upgrade
- **THEN** both attach to the same machine, and the kernel version of each is
  still recorded

#### Scenario: The hardware changes
- **WHEN** a run is recorded after the processor or memory changed
- **THEN** it attaches to a different machine, so its measurements are not
  compared against the previous hardware's

### Requirement: Measurements are ingested rather than measured
The store SHALL accept performance measurements produced elsewhere — the
Performance Lab's machine-readable export — and SHALL record each with its
metric name, value, unit, and the scenario that produced it. The testing suite
SHALL NOT measure performance itself.

#### Scenario: A Performance Lab export is ingested
- **WHEN** an export is ingested against a run
- **THEN** each measurement in it is stored with its metric, value, unit, and
  scenario, attached to that run

#### Scenario: An export cannot be read
- **WHEN** an export is malformed or names metrics the store does not recognise
- **THEN** ingestion fails with a message naming the problem, and no partial
  measurement set is left attached to the run

### Requirement: Automated test results are recorded against the run
The store SHALL record the outcome of each automated suite run against the build
under test — at minimum the suite's name, how many cases ran, how many failed,
and how long it took — so that a run record states what automated coverage said
about the same build the reviewer looked at.

#### Scenario: A suite result is ingested
- **WHEN** the result of an automated suite is ingested against a run
- **THEN** the suite name, case count, failure count, and duration are stored
  against that run

#### Scenario: A run with failing automated tests
- **WHEN** a run's stored results include a suite with failures
- **THEN** that is visible on the run without opening the suite's own output

### Requirement: A measurement is comparable against earlier runs on the same machine
The store SHALL answer, for a given metric, how its value in one run compares
with its values in earlier runs **on the same machine**, and SHALL NOT present a
comparison that spans machines as if it were a like-for-like one.

#### Scenario: Comparing a metric over time
- **WHEN** a metric from the current run is compared
- **THEN** the comparison is against that metric's earlier values on the same
  machine

#### Scenario: The only earlier values are from other machines
- **WHEN** no earlier run on this machine recorded the metric
- **THEN** the comparison reports that there is no baseline on this machine,
  rather than comparing against another machine's values

### Requirement: The schema is migrated forward
The store SHALL record its schema version and SHALL migrate an older store
forward when opened by a newer version of the suite, so that a store written
months earlier still opens and its history remains readable.

#### Scenario: An older store is opened
- **WHEN** a store written by an earlier version of the suite is opened
- **THEN** it is migrated to the current schema and its existing runs remain
  readable

#### Scenario: A store from a newer version is opened
- **WHEN** a store records a schema version newer than the running suite
  understands
- **THEN** the suite refuses to open it with a message saying so, rather than
  writing into a schema it does not understand

### Requirement: The store is local history, and the exported record is the contract
The store SHALL be local to the machine it runs on and SHALL NOT be the input to
any release gate. A completed run SHALL be exported to the run record under
`docs/development/quality/manual-runs/`, and the release gate SHALL continue to
read only that record.

#### Scenario: A release is gated
- **WHEN** the release gate checks manual verification
- **THEN** it reads the exported run record, and the store's presence or absence
  makes no difference to the result

#### Scenario: The store is lost
- **WHEN** a developer's store is deleted or a machine is replaced
- **THEN** past exported records remain valid and readable, and only local
  comparison history is lost

### Requirement: An exported record stays readable by the existing gate
An exported record SHALL carry the same fields the gate reads today — the
commit verified, the platform, the audio device, the mode, whether the geometry
sweep was covered, completeness, and every answer with its notes — and records
written by the retired terminal harness SHALL remain readable alongside it.

#### Scenario: A record from the new suite is gated
- **WHEN** the gate reads a record exported by the testing suite
- **THEN** it evaluates currency, completeness, mode, and failures exactly as it
  does for an existing record

#### Scenario: Historical records are read
- **WHEN** records written by the retired terminal harness are read
- **THEN** they still parse and still count as history

### Requirement: Tags and comments are exported without displacing the axes
The export SHALL carry each image's tags and comments alongside the three fixed
axes, recorded as additional detail rather than as verdicts, so that the
comparable core of a record is unchanged by the new review workflow.

#### Scenario: A tagged and commented image is exported
- **WHEN** a run whose images carry tags and comments is exported
- **THEN** the record contains the three axes for each surface as before, and
  the tags and comments appear as additional detail

### Requirement: Images are stored as files the store points at
Captured images SHALL be stored as files alongside the store, with the store
holding each image's location, dimensions, and content digest rather than the
image bytes.

#### Scenario: An image is served for review
- **WHEN** the review surface displays a capture
- **THEN** it reads the image file the store points at

#### Scenario: An image file is missing
- **WHEN** an image file referenced by the store is absent or its digest does
  not match
- **THEN** that capture is reported as missing rather than rendered as a blank
  or a broken image

### Requirement: Images can be pruned without losing what was decided
The suite SHALL provide a way to delete the image files of older runs while
keeping those runs' verdicts, tags, comments, measurements, and test results.

#### Scenario: Old images are pruned
- **WHEN** images older than the configured keep count are pruned
- **THEN** their runs' verdicts, tags, comments, measurements, and results
  remain, and those captures report their images as pruned rather than missing
