## Why

The tuner says "A4, +12 cents", but a musician thinks in keys and staff
positions. #297 shows where the note being sung or played sits:
- on a piano keyboard;
- on a staff;
- against the target, with a short trail so a scoop or a drift shows as a
  shape.

The maintainer approved designing and building it on 2026-11-08.

## What Changes

- **A new tool, Note View**, beside the tuner rather than a fourth tuner mode.
  The tuner already has three displays, and this one wants a different shape:
  a keyboard across the bottom, a staff, and the trail above. It shows:
  - **The keyboard:** two octaves around the note, with the key you're on lit
    and shaded by how far off you are, and the target's key marked.
  - **The staff:** your note and the target, side by side, in a clef chosen by
    hand or taken from the target's part.
  - **The trail:** the last three seconds of pitch drawn above the keys, so a
    slide is a slide.
  - **The keys held** on a MIDI keyboard light too.
- **Shared pitch tracking.** The tuner's `PitchTracker` moves to
  `src/platform/pitch/`, so the Note View smooths pitch exactly as the tuner
  does, from the same `SharedPitchAnalysis`. No new tracking and no audio
  work.
- **`ToolServices` gains the MIDI input**, so a tool can listen to the
  keyboard as the take recorder does.

## Capabilities

### New Capabilities
- `note-view`: the keyboard, staff and trail view of the note being sung or
  played, against the target.

## Impact

- New tool: a catalog entry, a factory, approved states, surfaces, the tool
  count, and `all-tools-docked`.
- `PitchTracker` moves, with its tests. The tuner's behaviour is unchanged.
- `ToolServices::midi`.
- A vocal range from #298 will widen the keyboard once it exists; it does not
  exist yet.
