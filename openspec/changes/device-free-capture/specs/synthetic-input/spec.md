## ADDED Requirements

### Requirement: An instance can decline an input device entirely
The application SHALL support being started in a mode where it opens no input
device at all, and in that mode the generated tone SHALL be the only producer
of samples into the analysis path.

One process at a time can hold the input device, so a verification run and a
live application compete for it and the live application loses. A capture
instance does not need the device it is competing for: the tone replaces a
device's input whenever one is asked for, so an instance that only ever analyses
a tone opens a device, reads from it, and discards every sample. Declining the
device outright removes the contention without removing anything that was being
used.

The mode is requested explicitly, not inferred from the control channel being
open, so that driving the application through that channel against a real
microphone stays possible.

#### Scenario: No device is opened
- **WHEN** the application is started with input declined
- **THEN** it opens no input device, and reports no device running

#### Scenario: An explicit device request is refused rather than honoured
- **WHEN** input is declined and something asks the service to initialise,
  reset to the default input, or apply a saved device state
- **THEN** no device is opened, and the request does not block the caller

#### Scenario: The tone still reaches the tools
- **WHEN** input is declined and a tone is asked for
- **THEN** the tools receive that tone, with a sample rate and a channel count,
  and input is reported as working

#### Scenario: A live application keeps its microphone
- **WHEN** a capture run of several device-declining instances is started while
  another process holds the input device
- **THEN** that process keeps the device for the whole run

### Requirement: Devices are still enumerated when input is declined
An instance that has declined to open an input device SHALL still enumerate the
available input devices.

Declining to open a device is not declining to know that devices exist. The
Settings input-device chooser is a verified surface, captured with the
instruction to switch device and switch back, and an empty list makes that
surface unanswerable rather than merely different. Enumeration does not hold the
device; opening it does.

#### Scenario: The chooser is populated
- **WHEN** input is declined and the Settings audio page is opened on a machine
  with an input device present
- **THEN** that device is listed

### Requirement: No capture worker is privileged by holding a device
Where several instances capture surfaces in parallel, the set of surfaces an
instance can honestly capture SHALL NOT depend on which instance holds an input
device.

A surface carrying a tone that is photographed by an instance hearing nothing is
a picture of a tool that has analysed silence, and it is counted as captured —
which is worse than a missing picture, because nothing reports it. Routing those
surfaces to whichever instance won the device was the way to avoid that while
the device was still being opened. With no instance opening one and the tone
always available, every instance can capture every surface and the routing has
nothing left to decide.

#### Scenario: Any worker can take any surface
- **WHEN** several device-declining instances divide a capture plan between them
- **THEN** any of them may capture any surface in it, including every surface
  that waits for a tone

#### Scenario: No surface is captured without the signal it waits for
- **WHEN** a surface that waits for a tone is captured by any instance
- **THEN** that instance was feeding the tone to its tools at the moment of
  capture
