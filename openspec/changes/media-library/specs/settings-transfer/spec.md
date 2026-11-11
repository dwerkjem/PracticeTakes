## ADDED Requirements

### Requirement: The library folder stays on its machine
The library folder SHALL be saved with this machine's settings and SHALL NOT be
carried in a `.ptsettings` bundle. Importing a bundle SHALL keep this machine's
library folder.

#### Scenario: Import settings from another computer
- **WHEN** a bundle exported on another machine is imported
- **THEN** the library folder is unchanged
