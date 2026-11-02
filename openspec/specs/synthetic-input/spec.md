# synthetic-input Specification

## Purpose

Defines the generated tone that gives an analysis tool something to work on when
no microphone is delivering samples: when it may be produced, which single source
is allowed to fill the analysis FIFOs at any moment, and what a tool is told about
input it is being fed synthetically. The tone is never audible — it exists so a
verification capture photographs a tool with a signal in it rather than one
waiting, and so that a machine with no input device is not a machine that cannot
be verified.
## Requirements
### Requirement: A tone can be generated without an input device
The application SHALL generate the synthetic tone whether or not an input device
is open, feeding it to the analysis path exactly as a device's samples are fed.

The tone is never heard. It exists to give a tool something to analyse for a
verification capture, and a device it does not otherwise need is a dependency
with nothing behind it — one that serialises every surface carrying a tone onto
whichever process won the device.

#### Scenario: A tone with no device
- **WHEN** a tone is asked for and no device is delivering samples
- **THEN** the tools receive that tone and analyse it

#### Scenario: A tone with a device
- **WHEN** a tone is asked for while a device is delivering samples
- **THEN** the tools receive the tone, as they do today

#### Scenario: No tone
- **WHEN** no tone has been asked for
- **THEN** nothing is generated, and the application behaves as it does now

### Requirement: Only one source fills the analysis path
The analysis FIFOs SHALL have one producer at any moment.

They are single-producer by construction, and a device that starts while the
tone source is mid-block would be a second — a corruption that no test would
see and that would present as a tool drawing nonsense.

#### Scenario: A device starts while a tone is being generated
- **WHEN** an input device begins delivering while the tone source is running
- **THEN** only one of them fills the FIFOs, and the samples in them stay intact

#### Scenario: The audio callback is unchanged in kind
- **WHEN** the callback takes its turn to fill the FIFOs
- **THEN** it does so without allocating, locking, or blocking

### Requirement: A tool cannot tell where its samples came from
A tool being fed a generated tone SHALL be told the same things a tool being fed
a device is told: its sample rate, its channel count, and that input is running.

A tool that renders "microphone disconnected" over a graph of a signal it is
being given is describing its own plumbing rather than what it has.

#### Scenario: What the tool is told
- **WHEN** a tool is fed a generated tone
- **THEN** it is given a sample rate and channel count, and reports input as
  working

#### Scenario: What the interface says
- **WHEN** a tone is being generated with no device
- **THEN** the interface does not report the input as disconnected

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
instance can honestly capture SHALL NOT depend on which instance happens to hold
an input device.

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

### Requirement: A surface about the device layer declares that it needs one
A surface whose subject is the input device itself SHALL be able to require a
real input device, and SHALL be captured by an instance that has one.

Declining the device is right for every surface whose signal is the tone, and
wrong for the few that are *about* the device. Captured by a declining instance,
the Settings input chooser showed no selected device, no channel list and no
advanced settings, and its question — whether switching device takes effect —
could only ever answer no, because a declining instance refuses device requests
by design. A picture of that is not a weaker version of the surface; it is a
picture of something else.

The requirement is on the surface declaring it, not on the worker discovering
it: the distinction is a property of what is being photographed, and a run must
hold the microphone for that surface alone rather than for its whole length.

#### Scenario: A surface that needs a device gets one
- **WHEN** a surface declaring that it needs an input device is captured
- **THEN** the instance capturing it has an input device open

#### Scenario: The device is given back afterwards
- **WHEN** the next surface captured does not declare that it needs one
- **THEN** the instance capturing it has declined the input device again

#### Scenario: Declaring it is the exception
- **WHEN** the set of surfaces is read
- **THEN** only those whose subject is the device layer declare the need, so a
  run holds the microphone for a bounded part of itself rather than throughout

