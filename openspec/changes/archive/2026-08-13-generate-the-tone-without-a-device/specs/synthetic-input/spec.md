## ADDED Requirements

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
