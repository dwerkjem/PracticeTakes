## Purpose

Defines the front door of the testing suite: every automated suite the project has in one place, runnable individually, by kind, or all at once, with what each needs built first, every result recorded against the same run, and history across runs graphed and shared.

## Requirements

### Requirement: The suite opens on a hub
Running the testing suite with no arguments SHALL open a hub listing everything
it can run, rather than printing usage. The hub SHALL be usable against an empty
store, because that is the state it is in the first time anyone opens it.

#### Scenario: The suite is started with no arguments
- **WHEN** the testing suite is run with no subcommand
- **THEN** the hub opens, listing every suite that can be run

#### Scenario: Nothing has ever been run
- **WHEN** the hub opens against a store with no runs in it
- **THEN** it still lists every suite and offers to run them, rather than
  reporting an error or an empty page

### Requirement: Everything runnable is offered in one place
The hub SHALL list every automated suite the project has — unit tests,
performance work, and user-interface checks — grouped by kind, with what each
one covers. The list SHALL be data, so that adding a suite is an edit to that
list rather than a change to the hub.

#### Scenario: A reviewer looks for what can be run
- **WHEN** the hub is open
- **THEN** every suite is listed under its kind, with a description of what it
  covers

#### Scenario: A suite is added
- **WHEN** a new suite is added to the list
- **THEN** it appears in the hub, is selectable, and runs, with no change to the
  hub itself

### Requirement: Suites can be run individually, by kind, or all at once
The hub SHALL allow any combination of suites to be selected and run, and SHALL
offer running everything, and running everything of one kind, without selecting
each item.

#### Scenario: Running a selection
- **WHEN** several suites are selected and run
- **THEN** each runs in turn and reports its own result

#### Scenario: Running everything
- **WHEN** the run-everything action is used
- **THEN** every suite runs, including those of every kind

#### Scenario: Running one kind
- **WHEN** the run-performance action is used
- **THEN** only the performance suites run

### Requirement: What a suite needs is built before it runs
The hub SHALL build the targets a selected suite depends on when they are
missing, rather than failing because a binary is absent, and SHALL build a shared
target once for a selection that needs it several times. It SHALL state before
the run which targets are missing, since building one is far slower than running
anything.

#### Scenario: A suite whose binary has never been built
- **WHEN** a suite that needs a build is run on a machine that has never built it
- **THEN** the target is built first, and then the suite runs

#### Scenario: Two suites needing the same target
- **WHEN** two selected suites depend on the same build target
- **THEN** it is built once

#### Scenario: A missing build is visible beforehand
- **WHEN** the hub is opened on a machine with no build
- **THEN** it says so before anything is run, so the wait is expected rather
  than mysterious

### Requirement: A run reports what it is doing while it does it
While a job runs, the hub SHALL report which step is running, how far through it
is, and the output as it arrives, so that a build taking minutes is
distinguishable from a hang.

#### Scenario: A long build is running
- **WHEN** a target is being compiled
- **THEN** the hub shows progress and the build's own output as it is produced

#### Scenario: A capture is running
- **WHEN** surfaces are being captured
- **THEN** the hub names the surface and resolution being captured and how many
  remain

### Requirement: A suite's verdict is its exit status
A suite SHALL be reported as failed when its command exits non-zero, whatever its
output could be parsed to say, and counts read out of output SHALL be recorded as
detail rather than as the verdict. A suite whose output cannot be parsed SHALL
still report the correct verdict.

#### Scenario: A suite fails with unparseable output
- **WHEN** a suite exits non-zero and its output does not match any known format
- **THEN** it is reported as failed

#### Scenario: A suite passes with unparseable output
- **WHEN** a suite exits zero and its output cannot be parsed
- **THEN** it is reported as passed, with its counts recorded as unknown rather
  than as zero failures out of zero cases

### Requirement: Results are recorded against the run
Each suite's outcome SHALL be stored against the run it was part of, and
performance suites SHALL additionally store their measurements, so that one run
answers what state a build was in across every kind of evidence.

#### Scenario: A mixed selection is run
- **WHEN** unit tests, performance work, and a UI capture are run together
- **THEN** all of their results attach to the same run

#### Scenario: A benchmark suite runs
- **WHEN** a performance suite produces timings
- **THEN** they are stored as measurements against the run, comparable against
  earlier runs on the same machine

### Requirement: A suite that needs a display is skipped without one
A suite requiring a real display SHALL be marked as such, and SHALL be skipped
with that reason rather than failing when no display is available.

#### Scenario: Running on a headless machine
- **WHEN** a suite needing a display is run with no display available
- **THEN** it is recorded as skipped with the reason, and the rest of the
  selection still runs

### Requirement: One job at a time
The hub SHALL refuse to start a second job while one is running, rather than
running two builds or two captures over each other.

#### Scenario: A second run is requested
- **WHEN** a run is requested while one is already in progress
- **THEN** it is refused with a message, and the running job is unaffected

### Requirement: Everything the hub does is available without a browser
Every action the hub offers SHALL also be available from the command line, so
that the suite works over a terminal-only session and can be scripted.

#### Scenario: Running suites without a browser
- **WHEN** suites are run from the command line
- **THEN** they run through the same job the hub uses and record the same results

#### Scenario: A machine with no browser
- **WHEN** the suite is used on a machine with no browser
- **THEN** every suite can still be run and every result read

### Requirement: History across runs is graphed
The hub SHALL present history across runs: the share of answered questions that
passed, per run over time, and each performance metric over time. A run in which
nothing was scored SHALL NOT appear as a point on the pass-rate line.

#### Scenario: Several runs exist
- **WHEN** the history view is opened
- **THEN** the pass rate per run is drawn over time, and each measured metric is
  drawn over time

#### Scenario: A run scored nothing
- **WHEN** a run recorded no verdicts
- **THEN** it contributes no point to the pass-rate line rather than appearing
  as zero or as a gap

#### Scenario: Skipped questions
- **WHEN** a run's pass rate is computed
- **THEN** skipped questions count against it, because an area nobody examined
  is not a pass

### Requirement: Each metric is graphed on its own scale
Performance SHALL be drawn as one chart per metric, each with its own scale and
its own unit. Two metrics SHALL NOT share an axis.

#### Scenario: Metrics in different units
- **WHEN** a run measures both milliseconds and nanoseconds
- **THEN** each is drawn in its own chart, labelled with its own unit

### Requirement: A trend never crosses machines
Every graph SHALL be for one machine, and SHALL offer the machines known to it
rather than merging them. A measurement from another machine SHALL NOT appear on
this machine's line.

#### Scenario: History from two machines
- **WHEN** history contains runs from two machines
- **THEN** the view shows one machine's runs and offers the other as a choice

### Requirement: History is shared as text, and images are not
The suite SHALL write one file per run into a version-controlled directory,
carrying the run's verdict counts, measurements, and automated results. Captured
images SHALL NOT be written there.

#### Scenario: History is synced
- **WHEN** history is synced
- **THEN** one file per run is written, and a run already written unchanged is
  not rewritten

#### Scenario: Two machines record runs
- **WHEN** two machines sync different runs and both are committed
- **THEN** the files merge without conflict, and both machines' runs are
  available to whoever pulls them

#### Scenario: Images stay local
- **WHEN** a run with captures is synced
- **THEN** no image, thumbnail, or image path is written to the shared directory

#### Scenario: A run known from both places
- **WHEN** a run exists both locally and in the shared directory
- **THEN** it is counted once, and the local copy is preferred because it may
  have gained answers since it was written out
