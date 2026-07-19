# Settings defaults

Settings schema version: **1**. Presets are identified by stable names and
resolved through the current schema, so future preset revisions do not change
or corrupt previously stored explicit values.

Changes remain session-only until **Save settings** is pressed. Saving records
the theme, selected audio-device setup, tuner controls, and the bounds of open
tool and Settings windows in JUCE's platform-appropriate per-user settings
directory. Those values are restored on the next launch.

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
- **Audio:** selects the current operating-system default input device.
- **Layout:** re-centers open tool and Settings windows at their preferred size.
- **All:** restores the theme, audio input, all open tools, and window layout.
