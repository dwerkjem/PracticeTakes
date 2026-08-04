## ADDED Requirements

### Requirement: Settings export produces a portable versioned bundle
The application SHALL export a human-readable, versioned settings bundle containing an explicit allowlist of Practice Takes-owned user preferences: appearance, microphone mute, input gain, saved audio-device configuration, fullscreen behavior, supported tool settings, feedback-invitation preference, the active workspace, and named workspaces.

#### Scenario: Successful settings export
- **WHEN** the user chooses an export destination and the file can be written
- **THEN** the application writes one complete settings bundle that can be imported by a compatible Practice Takes version

#### Scenario: Export includes workspace catalog
- **WHEN** the user exports settings after creating named workspaces and changing the active workspace
- **THEN** the bundle contains the active workspace and every named workspace in addition to the other supported settings

#### Scenario: Export is cancelled
- **WHEN** the user cancels the export destination chooser
- **THEN** no file is written and the application's settings remain unchanged

### Requirement: Export excludes private and operational records
The settings bundle SHALL NOT contain feedback drafts, feedback contact information, installation identifiers, usage counters, invitation history, submitted feedback, benchmark results, captured audio, analysis history, logs, credentials, or unrecognized properties from the native settings store.

#### Scenario: Native settings contain feedback data
- **WHEN** the native settings store contains a feedback draft, contact email, installation identifier, or usage history and the user exports settings
- **THEN** none of those values appear in the exported bundle

#### Scenario: Native settings contain an unknown property
- **WHEN** the native settings store contains a property outside the export allowlist
- **THEN** that property does not appear in the exported bundle

### Requirement: Import validates before changing settings
The application SHALL parse, identify, migrate if supported, normalize, and fully validate an imported bundle into a temporary candidate before changing live or persisted settings. Malformed files, unsupported file types, invalid required fields, and bundles from unsupported newer schema versions SHALL be rejected with an actionable error.

#### Scenario: Malformed import is rejected
- **WHEN** the user selects a malformed or structurally invalid settings file
- **THEN** the application reports that the file cannot be imported and leaves live and persisted settings unchanged

#### Scenario: Newer schema is rejected
- **WHEN** the user selects a bundle whose schema version is newer than the application supports
- **THEN** the application reports the compatibility problem and leaves live and persisted settings unchanged

#### Scenario: Supported older schema is migrated
- **WHEN** the user selects a valid bundle from an older schema for which a migration exists
- **THEN** the application migrates and validates the candidate before offering to apply it

### Requirement: Import requires informed confirmation
After validation, the application SHALL show that the import replaces supported current preferences, the active workspace, and the named-workspace catalog, and SHALL require explicit confirmation before applying the candidate.

#### Scenario: User confirms a valid import
- **WHEN** a bundle validates and the user confirms replacement
- **THEN** the application applies the imported supported settings and workspace catalog

#### Scenario: User cancels a valid import
- **WHEN** a bundle validates and the user cancels at the confirmation step
- **THEN** live and persisted settings remain unchanged

### Requirement: Confirmed import is atomic and immediately effective
The application SHALL apply a confirmed import as one logical transaction: either all validated supported settings become the live and persisted state, or the previous live and persisted state is restored. Successfully imported settings SHALL take effect without requiring an application restart.

#### Scenario: Confirmed import succeeds
- **WHEN** the user confirms a valid compatible bundle and persistence succeeds
- **THEN** all imported supported preferences and the imported active workspace are visible in the running application and survive restart

#### Scenario: Import application or persistence fails
- **WHEN** applying or saving any part of a confirmed import fails
- **THEN** the application reports the failure and restores the complete pre-import live and persisted settings

### Requirement: Imported machine-specific settings fall back safely
The application SHALL preserve portable settings when machine-specific audio-device or window-placement data cannot be used on the importing machine. It SHALL use an available default audio input and valid connected-display bounds instead of failing the import.

#### Scenario: Saved audio device is unavailable
- **WHEN** a valid imported bundle identifies an audio device that is not present
- **THEN** the remaining settings are imported and the application uses an available default input or its existing no-input recovery behavior

#### Scenario: Imported floating bounds target another display arrangement
- **WHEN** imported floating-window bounds are outside the connected displays
- **THEN** the settings import succeeds and the windows are constrained to connected displays
