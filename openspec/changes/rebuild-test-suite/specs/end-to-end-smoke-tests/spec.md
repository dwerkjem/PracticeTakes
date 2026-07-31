## ADDED Requirements

### Requirement: The built application launches headlessly
The repository SHALL provide a way to launch the built `PracticeTakes`
executable on Linux without a physical display, so that end-to-end verification
can run in CI.

#### Scenario: Launch on a machine with no display
- **WHEN** the smoke suite runs on a host with no attached display
- **THEN** the application starts under a virtual display server rather than
  failing to open a window

### Requirement: Startup is verified end to end
A smoke test SHALL launch the real built executable, confirm it reaches a
running state with its main window created, and SHALL fail if the application
exits, crashes, or fails to reach that state within a bounded time.

#### Scenario: The application starts normally
- **WHEN** the smoke test launches the executable
- **THEN** the main window is created within the timeout and the test passes

#### Scenario: The application crashes on startup
- **WHEN** a regression causes a crash before the main window appears
- **THEN** the smoke test fails and reports the exit status

#### Scenario: The application hangs on startup
- **WHEN** startup does not complete within the bounded time
- **THEN** the test fails on timeout rather than blocking the run indefinitely

### Requirement: A tool can be opened
A smoke test SHALL open at least one analysis tool in the launched application
and confirm it is present, verifying that the shell, the tool component, and
their wiring survive real construction rather than only unit-level use.

#### Scenario: A tool is opened after launch
- **WHEN** the smoke test opens an analysis tool
- **THEN** the tool is present in the running application

### Requirement: Shutdown is clean
A smoke test SHALL close the application and SHALL fail if it does not exit
within a bounded time or exits with a failure status, so that shutdown hangs and
teardown crashes are caught.

#### Scenario: Normal shutdown
- **WHEN** the smoke test closes the application
- **THEN** the process exits within the timeout with a success status

#### Scenario: Shutdown hangs
- **WHEN** a regression prevents the process from exiting
- **THEN** the test fails on timeout and the process is terminated so the run
  does not stall

### Requirement: Smoke tests run without a real audio device
The smoke suite SHALL run on a host with no working audio input, and the absence
of a capture device SHALL NOT by itself cause a smoke test to fail, since CI
runners have none.

#### Scenario: No capture device is present
- **WHEN** the smoke suite runs on a host with no audio input device
- **THEN** the application starts and the tests report on startup, tool opening,
  and shutdown rather than failing for lack of a device

### Requirement: Smoke tests are separable from the unit suite
The smoke suite SHALL be runnable independently of `PracticeTakesTests` and
SHALL NOT run as part of the default unit test invocation, so that the fast
suite stays fast.

#### Scenario: The default suite is run locally
- **WHEN** a developer runs the default test command
- **THEN** the smoke suite does not run
