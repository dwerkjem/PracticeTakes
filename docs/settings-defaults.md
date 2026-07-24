# Settings defaults

Settings schema version: **2**. Presets are identified by stable names and
resolved through the current schema, so future preset revisions do not change
or corrupt previously stored explicit values.

Settings are saved when **Save settings** is pressed and again during a normal
application shutdown. Saving records the theme, global mute and gain state,
selected audio-device setup, tuner controls, most recently used tool, and the
bounds of tool and Settings windows in JUCE's platform-appropriate per-user
settings directory. Window state is retained even after a window is closed.
Those values are restored on the next launch.

Writes use a temporary sibling file followed by atomic replacement. Schema-1
settings are migrated to schema 2 with safe defaults for the new mute, tuner
display, and recent-tool fields. Invalid individual values fall back to their
documented defaults. If the complete settings file is corrupt, it is renamed
with a `.corrupt` suffix and replaced with defaults. A file from a newer schema
is not automatically overwritten; pressing **Save settings** explicitly resets
the application-owned fields to the current schema.

A saved microphone setup remains the user's preference if that device is
temporarily missing. JUCE opens the current system default as a temporary
fallback while retaining the saved setup for future recovery.

## Global defaults

- Theme: Light
- Audio input: operating-system default input device
- Tool windows: centered at their preferred size when first opened
- Tuner window: 920 x 760
- Spectrogram window: 980 x 650
- Settings window: 760 x 650

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
- **Layout:** re-centers open tool and Settings windows at their preferred size.
- **All:** restores the theme, audio input, all open tools, and window layout.
