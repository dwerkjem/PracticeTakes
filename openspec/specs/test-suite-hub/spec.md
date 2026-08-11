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

### Requirement: A build can be asked for on its own
The hub SHALL offer to build each target, and all of them together, without
running any suite. The build is the slowest thing the hub does and the thing a
source change invalidates, so wanting one without also wanting a run is the
ordinary case rather than a special one.

A build on its own SHALL NOT create a run. A run records what was verified, and
a build verifies nothing; a run with no captures, no verdicts, and no results
under it would be indistinguishable in the history from one that failed to do
anything.

A build SHALL be subject to the same one-job-at-a-time rule as a run, and SHALL
report failure the way a run does rather than by raising.

#### Scenario: Building without running
- **WHEN** a build is asked for
- **THEN** that target is built, no suite is run, and no run appears in the
  history

#### Scenario: Building everything
- **WHEN** a build of every target is asked for
- **THEN** each target is built in turn, still without a run behind it

#### Scenario: Something is already running
- **WHEN** a build is asked for while a run or another build is under way
- **THEN** it is refused rather than started alongside

### Requirement: Whether something is built is shown as a colour
The hub SHALL show the state of each build target as a colour on the control
that builds it — one appearance for a target that is present and another for one
that is not — beside the controls that start a run.

Whether there is anything to run is the first question anyone opening the hub
has, and answering it in prose under a heading means answering it only for
people who read the prose.

The notices that describe a missing or out-of-date build SHALL carry the action
they describe, rather than naming a control elsewhere on the page.

#### Scenario: Nothing has been built
- **WHEN** a target is not built
- **THEN** its control says so by its appearance, and pressing it builds that
  target

#### Scenario: A build is older than the source
- **WHEN** a target was built before the newest source change
- **THEN** the hub says so, and the notice itself offers to rebuild that target

#### Scenario: A build is under way
- **WHEN** a build or a run is already going
- **THEN** the build controls are unavailable rather than queueing another

### Requirement: Stopping ends whatever the run is doing
A stop SHALL end the work in progress, whatever that work is: a build, a suite,
or a capture pass. It SHALL reach everything the run started, not only the
command the hub can see — a build is cmake, which is make, which is however many
compilers, and signalling the first of those leaves the machine working after
the page says the run has ended.

A command SHALL be asked to end before it is killed, and killed if it does not.
Waiting indefinitely for a well-behaved exit is the delay a stop exists to
avoid.

A suite that was stopped SHALL NOT be recorded as a result. A killed process
exits non-zero, and writing that down would put failures for tests that never
finished into the store and into the export the release gate reads. Suites that
never started SHALL be reported as stopped rather than left looking as though
they are still to come, and the run SHALL report that it was stopped rather than
that it failed.

Whatever finished before the stop SHALL be kept.

#### Scenario: Stopping during a build
- **WHEN** a run is stopped while a target is compiling
- **THEN** the compilation ends, no suite runs afterwards, and the run reports
  that it was stopped

#### Scenario: Stopping during a suite
- **WHEN** a run is stopped while a test suite is running
- **THEN** that suite ends, no result is recorded for it, and the suites behind
  it in the queue are reported as stopped rather than run

#### Scenario: What already ran
- **WHEN** a run is stopped after one suite has finished and before the next
- **THEN** the finished suite's result is kept and the rest are stopped

#### Scenario: The keyboard during a run
- **WHEN** Escape is pressed while any job is running
- **THEN** the run stops as though the stop control had been used, whatever the
  page was showing when the run began

### Requirement: The hub can be restarted from the page
The hub SHALL offer to restart itself, and that offer SHALL appear only where it
is needed — with the warning that the hub is out of date, and nowhere else.

A restart SHALL replace the running process rather than start a second one, so
the port is never contended, and SHALL preserve the options the hub was started
with. It SHALL NOT open a second browser window: the reason to press it is the
page it is pressed from.

A restart SHALL be refused while a run or a build is under way, naming what is
running, rather than being queued or taking that work down with it.

The page SHALL return itself to the restarted hub, and SHALL distinguish the new
process from the old one rather than reloading as soon as anything answers — a
restart keeps its process id and can have the port back within the second, and
reloading against the old process lands back on the fault being escaped.

#### Scenario: Restarting an out-of-date hub
- **WHEN** the restart is pressed on a hub running older code
- **THEN** the hub restarts with the same options, and the page returns to it
  once a different process is answering

#### Scenario: A hub that is up to date
- **WHEN** the hub is running current code
- **THEN** no restart control is shown

#### Scenario: Restarting while something is running
- **WHEN** a restart is asked for during a run or a build
- **THEN** it is refused and says what is running

### Requirement: A hub older than the code it serves says so
The hub SHALL report when it is running code older than the suite's source on
disk, on every view rather than only where a run is started.

The hub is a long-lived process that imports its modules once, while the page it
serves is read from disk on every request. A hub left open while the suite is
edited therefore answers a current page with stale code: a control the page has
gained posts to a route the process has never heard of, and data the page has
learned to read in a new shape arrives in the old one. Both present as a broken
feature rather than as a stale process, which is the failure this reports.

The comparison SHALL cover the suite's own modules and SHALL NOT cover the page
assets, which are re-read per request and so cannot fall out of step. A hub that
cannot determine what it started from SHALL say nothing rather than warn, because
a warning that appears on a current hub would teach reviewers to ignore it.

#### Scenario: The suite is edited while the hub is open
- **WHEN** a module of the suite is changed after the hub was started
- **THEN** every view of the hub says it is running older code and names
  restarting as the way out

#### Scenario: A hub started from the current source
- **WHEN** nothing has changed since the hub was started
- **THEN** no such warning appears anywhere in the hub

#### Scenario: Only the page assets change
- **WHEN** a file under the hub's web directory is changed but no module is
- **THEN** no warning appears, because the page is served from disk each time
