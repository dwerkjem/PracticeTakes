## Why

Practice Takes can arrange tools into docked, tabbed, split, and floating presentations, but that workspace is lost at restart and first launch presents no useful working arrangement. Users also need a supported way to move or back up their application, audio, tool, and workspace settings without copying implementation-specific settings files.

## What Changes

- Add four useful built-in workspaces for vocal warm-up, pitch practice, spectrum analysis, and performance preparation, with pitch practice as the first-launch fallback.
- Persist and restore the active workspace, including dock topology, tab selection, split ratios, floating-window bounds, focused tool, and supported per-tool settings.
- Let users save, apply, rename, overwrite, and delete named workspace layouts.
- Recover usable portions of layouts containing missing or renamed tools and fall back safely when layout data is invalid.
- Reset the active workspace to a built-in layout without resetting unrelated global preferences.
- Export Practice Takes-owned application, appearance, audio, tool, active-workspace, and named-workspace settings to a portable versioned file.
- Import a supported settings file through a validated, confirmed, atomic replacement that leaves existing settings unchanged if validation or application fails.
- Document import/export compatibility, machine-specific audio-device fallback, and data intentionally excluded from settings transfer.

## Capabilities

### New Capabilities
- `workspace-layout-restoration`: Built-in workspaces, active-session restoration, named-layout management, layout recovery, and reset behavior.
- `settings-transfer`: Portable, versioned export and safe import of owned application and workspace settings.

### Modified Capabilities

None.

## Impact

- Extends the pure workspace state model and its JUCE presentation adapters to retain split ratios and active tabs and to apply validated snapshots transactionally.
- Extends application settings persistence with a versioned structured workspace catalog while preserving existing schema migration and corruption recovery.
- Adds workspace management and settings import/export commands, dialogs, confirmation, and error reporting to the application shell and settings UI.
- Adds unit and UI-focused coverage for presets, round trips, migration, renamed or missing tools, malformed files, conflict handling, and restart restoration.
- Updates user documentation for workspace management and portable settings files.
