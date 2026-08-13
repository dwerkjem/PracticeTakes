# audio-device-recovery Specification

## Purpose

Defines how the application reopens an input device that has gone away, without
the interface waiting on it. Opening a device is a blocking call that can take
seconds — or never return, when another process holds the device — so recovery
happens off the message thread, at most one attempt at a time, reported while it
runs and abandoned rather than waited for at shutdown. Also draws the line
between an automatic recovery and a device the user explicitly chose, which are
not the same event and must not be treated as one.

## Requirements
### Requirement: The interface never waits for a device to open
The application SHALL NOT block its message thread on opening an input device it
was not explicitly asked to open.

Opening a device can wait without limit: the backend may be waiting on another
holder of the device, and there is no timeout to reach. Waiting for that on the
thread that repaints, opens menus, and answers the control channel turns a busy
device into an application that cannot be used or closed.

An automatic recovery SHALL therefore run on a thread of its own, and the timer
that decides to attempt one SHALL return without waiting for the result.

#### Scenario: The device is held by something else
- **WHEN** an input device cannot be opened because another process holds it
- **THEN** the window continues to repaint, respond, and close

#### Scenario: A device that never opens
- **WHEN** an attempt to open the input device does not return
- **THEN** the application remains usable, and remains able to quit

### Requirement: One recovery is attempted at a time
The application SHALL NOT begin an automatic recovery while one is in progress.

The scan that decides to recover runs every two seconds while there is no usable
input — which is exactly the state a stuck open leaves it in. Without this, a
device that is slow to open produces a queue of attempts, each waiting on what
the one before it is waiting on.

#### Scenario: A recovery is already running
- **WHEN** the scan runs while a recovery is in progress
- **THEN** it asks for no further recovery

#### Scenario: A recovery that finished
- **WHEN** a recovery completes and the input is still unusable
- **THEN** a later scan may attempt another

### Requirement: A recovery in progress is reported
The application SHALL report that it is trying to open an input device, as a
state distinguishable from having none.

"No microphone detected" and "the microphone is not answering" are different
situations with different answers, and the second is the one where somebody
would otherwise conclude the application had crashed.

The report SHALL NOT require dismissal or block use of anything else.

#### Scenario: Waiting on a device
- **WHEN** a recovery has been running for long enough to notice
- **THEN** the interface says the input device is being opened, rather than that
  there is none

#### Scenario: The recovery succeeds
- **WHEN** the device opens
- **THEN** the report goes, and input works without further intervention

### Requirement: Shutdown does not wait for a recovery
The application SHALL close while a recovery is in progress, without waiting for
it to finish.

A recovery may be inside a call that never returns; waiting for it at shutdown
would reproduce the freeze being fixed at the moment somebody tries to escape
it.

Anything a recovery in progress may touch after the service is gone SHALL
outlive it.

#### Scenario: Quitting while the device is being opened
- **WHEN** the application is closed during a recovery that has not returned
- **THEN** it closes

### Requirement: An explicit device change is not a recovery
Choosing an input device SHALL take effect as directly as it does now.

This governs the automatic retry, which nobody asked for and which happens every
two seconds. Somebody choosing a device in Settings has asked for exactly that
and is watching it happen, and routing it through a background thread would buy
nothing and cost the connection between the click and the result.

#### Scenario: Choosing a device
- **WHEN** an input device is chosen in Settings
- **THEN** it is applied, and the result is reported as it is today

