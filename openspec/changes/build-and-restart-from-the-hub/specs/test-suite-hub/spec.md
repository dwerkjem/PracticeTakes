## ADDED Requirements

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
