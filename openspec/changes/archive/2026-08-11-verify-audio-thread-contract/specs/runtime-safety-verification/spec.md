## ADDED Requirements

### Requirement: Memory errors and undefined behaviour are observed on every pull request
Every pull request SHALL build the C++ test suite with AddressSanitizer and
UndefinedBehaviorSanitizer enabled and run it to completion, in a build tree
separate from the ordinary one. The check SHALL fail the pull request on any
report that is not covered by a recorded suppression.

#### Scenario: A pull request introduces a use-after-free
- **WHEN** a change frees memory that a later read reaches
- **THEN** the sanitizer check fails on that pull request and names the file and
  line of both the free and the read

#### Scenario: A pull request introduces signed integer overflow
- **WHEN** a change performs an arithmetic operation whose result is not
  representable
- **THEN** the sanitizer check fails on that pull request

#### Scenario: An ordinary pull request with no findings
- **WHEN** a pull request introduces no memory error and no undefined behaviour
- **THEN** the sanitizer check passes without requiring any new suppression

### Requirement: Data races are observed against the concurrency suite
The concurrency tests tagged `[.load]` SHALL be built with ThreadSanitizer in
their own build tree and run on a schedule and on every push to the default
branch. They SHALL NOT be required to pass before a pull request merges, because
the soak is the expensive leg; running on pushes to the default branch SHALL
make a failure attributable to a specific commit rather than to a week of them.

#### Scenario: A race is introduced into the sample FIFO
- **WHEN** a change removes or weakens the synchronisation between the FIFO's
  producer and consumer
- **THEN** the scheduled ThreadSanitizer run reports a data race naming both
  accesses

#### Scenario: A race reaches the default branch
- **WHEN** a commit containing a data race is pushed to the default branch
- **THEN** ThreadSanitizer runs against that commit, so the failure identifies
  which commit introduced it

### Requirement: A failing scheduled run enters the issue queue
A scheduled verification run that fails SHALL open an issue naming the workflow,
the run URL, and the failing step, labelled for triage. It SHALL NOT rely on the
Actions tab or on default notification email as its only report. If an open
issue already records that workflow's failure, the run SHALL add to it rather
than open a duplicate.

#### Scenario: The nightly run goes red
- **WHEN** a scheduled sanitizer run fails
- **THEN** an issue exists describing the failure, discoverable without opening
  the Actions tab

#### Scenario: The nightly run fails again before the first is resolved
- **WHEN** a scheduled sanitizer run fails while an issue for that workflow's
  failure is already open
- **THEN** no duplicate issue is created

### Requirement: The audio callback is executed under observation
The real-time audio callback SHALL be invoked by the automated test suite. A
verification tool that observes only code which runs SHALL NOT be treated as
covering the audio-thread contract while no test executes the callback.

#### Scenario: The callback is driven by a test
- **WHEN** the test suite runs
- **THEN** `audioDeviceIOCallbackWithContext` is invoked with input and output
  buffers, and its behaviour is asserted

#### Scenario: A verification tool reports success against unexecuted code
- **WHEN** a real-time verification tool passes but no test invokes the callback
- **THEN** that result is not evidence the contract holds

### Requirement: Real-time safety violations fail the pull request that introduces them
The audio callback SHALL be annotated as non-blocking in a way that a real-time
verification tool enforces, and that verification SHALL run on every pull
request. Allocation, lock acquisition, and blocking system calls inside the
callback SHALL fail the check. The annotation SHALL NOT break compilation with a
toolchain that does not recognise it.

#### Scenario: A change allocates on the audio thread
- **WHEN** a pull request introduces a heap allocation reachable from the audio
  callback
- **THEN** the real-time check fails on that pull request

#### Scenario: A change takes a lock on the audio thread
- **WHEN** a pull request introduces a mutex acquisition reachable from the
  audio callback
- **THEN** the real-time check fails on that pull request

#### Scenario: The project is built with a toolchain lacking the tool
- **WHEN** the project is compiled with a toolchain that does not support the
  non-blocking annotation
- **THEN** compilation succeeds and the annotation is inert

### Requirement: Ignored findings are recorded rather than silenced wholesale
Findings originating outside this repository's source SHALL be handled by a
version-controlled suppression file in which every entry carries a comment
explaining why it is suppressed. A sanitizer SHALL NOT be disabled wholesale to
avoid third-party noise.

#### Scenario: A third-party library leaks
- **WHEN** a dependency reports a leak this project cannot fix
- **THEN** a suppression entry exists for it, with a comment naming the
  dependency and the reason

#### Scenario: A leak in this repository's own source
- **WHEN** a leak originates in a file under `src/`
- **THEN** no suppression covers it and the check fails
