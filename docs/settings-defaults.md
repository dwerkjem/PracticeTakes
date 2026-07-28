# Settings defaults

Settings schema version: **5**. Portable settings schema version: **2**.
Presets are identified by stable names and
resolved through the current schema, so future preset revisions do not change
or corrupt previously stored explicit values.

Settings are saved when **Save settings** is pressed and again during a normal
application shutdown. Saving records the theme, global mute and gain state,
selected audio-device setup, tuner controls, fullscreen mode, most recently
used tool, the active workspace, named workspaces, and relevant window bounds
in JUCE's platform-appropriate per-user settings directory. Window and
workspace state are restored on the next launch.

Writes use a temporary sibling file followed by atomic replacement. Older
settings are migrated to the current schema with safe defaults for newer
fields. Invalid individual values fall back to their
documented defaults. If the complete settings file is corrupt, it is renamed
with a `.corrupt` suffix and replaced with defaults. A file from a newer schema
is not automatically overwritten; pressing **Save settings** explicitly resets
the application-owned fields to the current schema.

A saved microphone setup remains the user's preference if that device is
temporarily missing. JUCE opens the current system default as a temporary
fallback while retaining the saved setup for future recovery.

## Workspaces

Four immutable built-in workspaces are always available from **Tools →
Workspaces**: Vocal Warm-up, Pitch Practice, Spectrum Analysis, and Performance
Preparation. Applying a built-in creates a working copy, so later divider,
tab, floating-window, focus, and supported tool-setting changes do not modify
the built-in definition.

**Save workspace as** stores the current arrangement as an independent named
workspace. Named workspaces can be applied, overwritten, renamed, and deleted.
Resetting the workspace replaces only the active arrangement with Pitch
Practice. It preserves named workspaces and global appearance, audio,
fullscreen, and feedback preferences.

## Portable settings

**Export settings** writes a versioned, human-readable `.ptsettings` bundle
using an atomic file replacement. It includes supported appearance, microphone
mute and gain, saved audio-device preference, fullscreen behavior, tuner and
workspace tool settings, feedback-invitation preference, the active workspace,
and named workspaces.

The bundle excludes feedback drafts and contact details, installation
identity, use counters, invitation history, unknown internal properties, logs,
benchmarks, and analysis data.

**Import settings** reads and validates the complete bundle before changing
the application. Supported older bundles are migrated. Malformed bundles and
bundles from a newer unsupported schema report a compatibility error. A valid
bundle shows its theme, audio preference, active workspace, and named-workspace
count and requires confirmation. Confirming replaces all supported preferences
and named workspaces; it does not merge them. The imported state is applied
immediately and persisted atomically. If application or persistence fails, the
previous live state and settings file are retained.

Unavailable saved audio devices fall back to an available system input, and
window positions from disconnected displays are moved into a visible display.
These recoveries do not reject an otherwise valid import.

## Global defaults

- Theme: Light
- Fullscreen mode: Normal fullscreen
- Audio input: operating-system default input device
- Active workspace: Pitch Practice
- Split divider: equal initial allocation, resizable within tool minimum sizes
- Floating tool windows: centered at their preferred size
- Tuner window: 920 x 760
- Spectrogram window: 980 x 650
- Harmonic Analyzer window: 980 x 700
- Settings window: 760 x 650

Normal fullscreen keeps the Practice Takes title bar visible. Kiosk fullscreen
hides the title bar for an immersive view and reveals it when the pointer reaches
the top edge of the screen. Both modes ask the operating system to hide its
desktop bars while fullscreen is active.

## Tuner defaults

- Display: Graph
- Pitch easing: 0.35
- Average window: 5 samples
- Note-switch threshold: 0.55 semitones
- Dropout hold: 4 frames
- Graph duration: 20 seconds
- Advanced settings: collapsed
- Pitch and graph history: empty

The spectrogram has no user-adjustable per-tool settings in schema version 1.

## Presets

### Voice practice

- Pitch easing: 0.25
- Average window: 7 samples
- Note-switch threshold: 0.45 semitones
- Dropout hold: 7 frames
- Graph duration: 30 seconds

### General instrument practice

Uses the tuner defaults listed above.

## Reset scopes

- **Current tool:** restores only the most recently opened tool's controls and
  transient analysis history.
- **Audio:** unmutes the microphone, restores unity gain, and selects the
  current operating-system default input device.
- **Layout:** replaces only the active workspace with Pitch Practice while
  preserving named workspaces and global preferences.
- **All:** restores the theme, audio input, all open tools, and window layout.
