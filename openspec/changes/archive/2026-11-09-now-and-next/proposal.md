## Why

During a take a singer reads ahead: what am I on, how long is left, what is
next, and where do I breathe. The Score View shows all of it at once, small,
with the cursor moving through it. #41 asks for a display built for the moment
of performance:
- the current note large, with its name, staff position, length, lyric and
  time left;
- a strip of the notes coming;
- rests, ties, repeated notes and phrase starts shown as what they are;
- a high-contrast mode readable from a music stand.

The maintainer chose it as the next piece on 2026-11-09.

## What Changes

- **A part's timeline** (`score::PartTimeline`, JUCE-free). A part's first voice
  becomes the things a singer does, in order:
  - a note, with its tied continuations folded into one long note;
  - a rest, with consecutive rests merged.

  Each is marked with its lyric, and whether it repeats the note before or
  starts a phrase.
- **A new tool, Now & Next** (`now-next`):
  - the current note large, with a remaining-time bar that runs down smoothly
    with the transport;
  - the next notes as a strip that steps rather than scrolls, its length
    chosen in the tool's menu;
  - a rest shown as a rest, a repeated note marked "again", and a phrase start
    as a breath mark and a gap;
  - a high-contrast mode, and a compact layout when it is small;
  - it follows the target's part by default, or any part chosen in its menu.
- **The current note changes when it is heard**: at the clock's frame less
  the output latency, which #38 will calibrate.
- **The shared target follows the score as it plays.** While the clock moves
  through a score, a target that came from the score becomes the note being
  sung in the target's part, so the tuner and the Note View aim at it. A
  target the player chose is left alone. Settings → Practice turns this off.

## Capabilities

### New Capabilities
- `now-and-next`: the part timeline, the tool, and the target following the
  clock.

## Impact

- New tool: a catalog entry, a factory, approved states, surfaces, the tool
  count, and `all-tools-docked`.
- `src/platform/score/PartTimeline`, and a small staff drawn in the tool.
- A setting, `practice.targetFollowsScore`, with its toggle.
- MIDI files: there is no MIDI importer yet. The tool reads the score model,
  which a MIDI importer will produce, so nothing here is MusicXML-specific.
