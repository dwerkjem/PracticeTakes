## MODIFIED Requirements

### Requirement: Settings export produces a portable versioned bundle
The application SHALL export a human-readable, versioned settings bundle containing every Practice Takes-owned user setting: appearance (theme, text size, interface language, note naming and octave numbering), microphone mute, input gain, the saved audio-device configuration including the output device, the metronome and count-in, score playback, the practice settings (reference pitch, intonation limits, minimum input level, and the target note's options), the MIDI input selection and pedal threshold, fullscreen behavior, supported tool settings, feedback-invitation preference, the active workspace, and named workspaces.

#### Scenario: Successful settings export
- **WHEN** the user chooses an export destination and the file can be written
- **THEN** the application writes one complete settings bundle that can be imported by a compatible Practice Takes version

#### Scenario: Export includes workspace catalog
- **WHEN** the user exports settings after creating named workspaces and changing the active workspace
- **THEN** the bundle contains the active workspace and every named workspace in addition to the other supported settings

#### Scenario: Export includes language and naming
- **WHEN** the user exports settings after choosing an interface language, a note-naming convention and an octave numbering
- **THEN** the bundle contains all three

#### Scenario: Export includes devices
- **WHEN** the user exports settings with an input device, an output device and a MIDI keyboard selected
- **THEN** the bundle names all three

#### Scenario: Export is cancelled
- **WHEN** the user cancels the export destination chooser
- **THEN** no file is written and the application's settings remain unchanged

### Requirement: Export excludes private and operational records
The settings bundle SHALL NOT contain feedback drafts, feedback contact information, installation identifiers, usage counters, invitation history, submitted feedback, benchmark results, captured audio, analysis history, logs, credentials, the target note chosen for practice, or unrecognized properties from the native settings store.

#### Scenario: Native settings contain feedback data
- **WHEN** the native settings store contains a feedback draft, contact email, installation identifier, or usage history and the user exports settings
- **THEN** none of those values appear in the exported bundle

#### Scenario: Native settings contain an unknown property
- **WHEN** the native settings store contains a property outside the export allowlist
- **THEN** that property does not appear in the exported bundle

#### Scenario: An import keeps the chosen target note
- **WHEN** the user confirms an import while a target note is chosen
- **THEN** the same target note is still chosen afterwards

### Requirement: Import requires informed confirmation
After validation, the application SHALL show that the import replaces every current setting, the active workspace, and the named-workspace catalog, and SHALL require explicit confirmation before applying the candidate. When the bundle's interface language differs from the current one, the confirmation SHALL say that the language changes when the application next starts.

#### Scenario: User confirms a valid import
- **WHEN** a bundle validates and the user confirms replacement
- **THEN** the application applies the imported settings and workspace catalog

#### Scenario: User cancels a valid import
- **WHEN** a bundle validates and the user cancels at the confirmation step
- **THEN** live and persisted settings remain unchanged

#### Scenario: The bundle changes the language
- **WHEN** a bundle validates whose interface language differs from the current one
- **THEN** the confirmation says the language changes when Practice Takes next starts

#### Scenario: An older bundle resets nothing it does not carry
- **WHEN** the user imports a valid bundle exported before every setting travelled
- **THEN** the settings it does not contain, and the output device it does not name, keep this machine's values

### Requirement: Confirmed import is atomic and immediately effective
The application SHALL apply a confirmed import as one logical transaction: either all validated supported settings become the live and persisted state, or the previous live and persisted state is restored. Successfully imported settings SHALL take effect without requiring an application restart, except the interface language, which every window takes when it is built and which therefore takes effect at the next launch.

#### Scenario: Confirmed import succeeds
- **WHEN** the user confirms a valid compatible bundle and persistence succeeds
- **THEN** all imported settings other than the interface language, and the imported active workspace, are visible in the running application, and all of them survive restart

#### Scenario: Import application or persistence fails
- **WHEN** applying or saving any part of a confirmed import fails
- **THEN** the application reports the failure and restores the complete pre-import live and persisted settings

### Requirement: Imported machine-specific settings fall back safely
The application SHALL preserve portable settings when machine-specific audio-device, MIDI-device or window-placement data cannot be used on the importing machine. It SHALL use an available default audio input and output, hold an absent MIDI keyboard as a selection that opens when it is connected, and use valid connected-display bounds instead of failing the import.

#### Scenario: Saved audio device is unavailable
- **WHEN** a valid imported bundle identifies an audio input or output device that is not present
- **THEN** the remaining settings are imported and the application uses an available default device or its existing no-input recovery behavior

#### Scenario: Saved MIDI keyboard is not connected
- **WHEN** a valid imported bundle names a MIDI keyboard that is not connected
- **THEN** the remaining settings are imported and the keyboard opens when it is connected

#### Scenario: Imported floating bounds target another display arrangement
- **WHEN** imported floating-window bounds are outside the connected displays
- **THEN** the settings import succeeds and the windows are constrained to connected displays
