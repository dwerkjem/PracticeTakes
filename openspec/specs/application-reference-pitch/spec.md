# application-reference-pitch Specification

## Purpose
One tuning reference, A4 in hertz, owned by the application and used by every
conversion between pitch and frequency.
## Requirements
### Requirement: One reference pitch for the application
The application SHALL own one reference pitch, A4 in hertz, from 415 to 466,
440 by default. It SHALL be settable in Settings → Practice and remembered, and
every conversion between pitch and frequency SHALL use it.

#### Scenario: Tuning to A442
- **WHEN** the reference is set to 442 Hz and a 442 Hz tone is played
- **THEN** the tuner reads A4, in tune

#### Scenario: A file from before
- **WHEN** settings saved before the reference pitch existed are loaded
- **THEN** the reference is 440 Hz
