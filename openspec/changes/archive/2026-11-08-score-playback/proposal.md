## Why

The practice clock keeps time and clicks, but it can't play the music. Two MVP
steps need it to:
- **#201**, hands separately: a pianist wants to hear the left hand while
  practising the right, or to mute the hand they're playing.
- **#286**, accompaniment: a singer needs to hear the rest of the score while
  their own part is silent.

The rest of #201 already exists in the clock: a looped bar range that survives
a tempo change, a count-in at the loop's start, and a reduced tempo.

The maintainer chose (2026-11-08) a **bundled General MIDI SoundFont** for the
sound, over a plain built-in synthesiser or a MIDI output.

## What Changes

- **The score sounds through the clock.**
  - Its notes are played by a SoundFont player inside the existing
    `ClockRenderer`.
  - They are placed at clock frames, so they follow the tempo scale, the
    score's own tempo changes, loops and seeks exactly as the clicks do.
  - Silent during the count-in.
  - Bounded on the audio thread: preallocated voices, a capped number of
    events per block, and no allocation.
- **A mix.** Each part can be heard or not. A part with two staves (a piano)
  has its staves, the hands, muted or soloed separately. Turning on **Play the
  score** is what makes anything sound; it is off by default, so the clock
  sounds as it does today until someone turns it on. A volume for the score
  sits beside the metronome's.
- **TinySoundFont** (MIT, one header) is the player, and **GeneralUser GS
  v2.0.3** (32 MB, free to use and redistribute in software) is the sound.
  Both are fetched at configure time at pinned commits, never committed. The
  SoundFont is installed beside the application, and is loaded on a worker
  thread after launch. Without it the clock still works, and the mix says
  playback is unavailable and why.
- **Each part's instrument** comes from the file's `<midi-program>`, which the
  importer accepts today and drops. The model gains `Part::midiProgram`. A part
  without one plays as an acoustic grand piano.
- **During a take** the mix plays on, so a singer hears the accompaniment. The
  microphone hears it too; #38's calibration is where that is measured.

## Capabilities

### New Capabilities
- `score-playback`: the score's notes sounding with the clock, the mix, the
  SoundFont and where it comes from.

### Modified Capabilities
- `practice-clock`: the renderer gains a sound source beside the metronome.
- `normalized-score-model`: `Part` gains `midiProgram`.
- `musicxml-import`: `<midi-program>` is read, not dropped.

## Impact

- **The audio-thread contract** gains a second sound source, under the same
  rules. It is checked by the RealtimeSanitizer job, which loads the SoundFont
  and plays notes through the real callback.
- **Packaging** installs one more file on every platform (Linux
  `share/practice-takes`, Windows beside the executable, macOS the bundle's
  `Resources`), and its licence beside it. Installers grow by about 32 MB.
- **Configure-time download** of the SoundFont repository (about 32 MB) on a
  clean build.
- **Not in this change:**
  - a MIDI file as accompaniment (#34);
  - dynamics shaping velocity;
  - choosing another SoundFont;
  - saving the mix in a session (#39).
