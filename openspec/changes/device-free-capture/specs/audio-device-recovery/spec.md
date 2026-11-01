## ADDED Requirements

### Requirement: An instance that has declined input attempts no recovery
An instance started with its input device declined SHALL NOT attempt an
automatic recovery, and SHALL NOT scan for device changes.

Recovery exists to reopen a device the application is supposed to have. An
instance that has declined one is not missing a device, so every recovery it
could attempt would be refused at the point the device work is started — once
per timer tick, for the life of the process. Stopping the scan as well is what
keeps that from being a loop that does nothing noisily.

The refusal at the device-work funnel is what guarantees no device is opened;
stopping the scan is for quiet. Both are required, and removing either on the
grounds that the other covers it would be wrong — the funnel without the scan
guard is correct but wasteful, and the scan guard without the funnel leaves the
explicit-request paths able to open a device.

#### Scenario: The timer finds no device and does nothing
- **WHEN** input is declined and the service's timer ticks with no input device
  present
- **THEN** no recovery is attempted, and none is reported in flight

#### Scenario: Declining does not weaken recovery elsewhere
- **WHEN** an ordinary instance, which has not declined input, loses its device
- **THEN** it recovers exactly as it does today, off the message thread, one
  attempt at a time, reported while it runs
