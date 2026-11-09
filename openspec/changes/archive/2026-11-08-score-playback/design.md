## Context

`ClockRenderer` runs inside the audio callback. It keeps the position, counts
in, loops and synthesises clicks. A table of up to 512 segments, published into
one of two preallocated slots, turns ticks into frames at the current tempo
scale. The output path, opened as two channels with an input-only fallback,
came with the practice clock.

This change adds a second sound source to the renderer: the score itself.

## Goals / Non-Goals

**Goals:**
- Notes sound at their clock frames, so tempo scale, tempo changes, loops,
  seeks and the count-in behave exactly as the clicks do.
- Mute and solo per staff, which the importer and engraver already treat as a
  hand.
- Nothing allocates, locks or waits on the audio thread.
- No change for anyone who doesn't turn playback on.

**Non-Goals:**
- Dynamics, articulation or pedalling in the sound.
- A MIDI file as the source (#34).
- A SoundFont of the user's choosing.
- Saving the mix in a session (#39).

## Decisions

### 1. TinySoundFont, wrapped, with its allocations moved off the audio thread

`tsf.h` (MIT, one header) loads SF2 and renders it. Nothing else in the
application knows it exists: `transport::ScoreSynth` wraps it.

- **On a worker thread:** loading the file, `tsf_set_max_voices(96)`, and
  creating all 64 channels up front.
- **Read from the source:** TinySoundFont allocates in only three places: a
  new voice when none is free and no maximum is set, a channel used for the
  first time, and loading. With a maximum and the channels created, note-on,
  note-off, a channel's preset, `tsf_set_output` and rendering do not
  allocate.
- **When no voice is free,** a note on its release is taken. If there is none,
  the new note is dropped, which beats allocating.
- **Memory:** TinySoundFont keeps samples as floats, so the 32 MB file takes
  about 64 MB. That is acceptable for a desktop application, and is said here
  so nobody is surprised by it.

### 2. GeneralUser GS, fetched, installed beside the application

GeneralUser GS v2.0.3 is a 32 MB General MIDI bank.
- **Licence:** "use it in your software projects", with redistribution
  allowed. The author notes he can't vouch for every sample's origin; that is
  recorded in `tools/packaging/sounds/README.md` for the maintainer's release
  review.
- **Fetched, not committed:** CMake fetches it at a pinned commit, like JUCE
  and Catch2.
- **Not embedded:** a 32 MB binary-data source is 111 MB of C++. It took 58 s
  to compile with GCC at -O0, and would cost more on MSVC, on every clean CI
  build. The application would also hold a second copy in memory.
- **Installed:** a POST_BUILD step copies it to `bin/sounds/` for development
  runs. Install rules put it in `share/practice-takes/sounds/` on Linux,
  `bin/sounds/` on Windows, and the bundle's `Contents/Resources/sounds/` on
  macOS, each with its licence. The shell looks in those places relative to the
  executable.
- **Missing:** the clock works without it, and the mix says playback is
  unavailable, naming the file.
- **Loading:** about 0.3 s on a worker thread after launch. The practice
  clock's timer picks it up and hands it to the renderer.

### 3. The notes travel with the table

The renderer reads notes from the same slot as the table, so the two always
agree.
- **Built:** `playbackEventsOf(score, table)` (JUCE-free) turns every pitched,
  non-grace note into a note-on and a note-off, at clock frames. A tied note
  becomes one note across its ties.
- **Tracks:** each (part, staff) pair is a track. Each track has its own
  synthesiser channel and the part's program: `<midi-program>` minus one, or
  0, an acoustic grand piano.
- **Ordering:** events are sorted by frame, with offs before ons at the same
  frame, so a repeated note re-strikes.
- **Limits:** at most 64 tracks. Velocity is 80 for now.
- **Building:** done on the message thread whenever the table is, into the
  slot the renderer isn't using.

### 4. Dispatch, bounded

In `advance`, each span the clock plays (never the count-in) dispatches the
events in `[position, position + length)`:
- from a cursor into the event list;
- found again by binary search after any discontinuity: a seek, a wrap, a new
  table, or play starting;
- rendering the synthesiser up to each event's offset, then applying it.

Bounds:
- **Events:** at most 512 per block. More are skipped and counted.
- **Rendering:** a 4,096-frame stereo scratch, so a larger block renders in
  chunks.

**Every discontinuity,** stop and pause included, releases every note. A loop
therefore never leaves a note hanging past its end.

### 5. The mix is a mask

`PlaybackMix` (JUCE-free) holds:
- **Play the score**: off by default, and saved;
- a volume;
- mute and solo per track.

`audibleTracks()` folds them into a 64-bit mask:
- a track sounds when playback is on, it isn't muted, and either nothing is
  soloed or it is.

The renderer reads the mask atomically each block:
- a track that has just become silent is released;
- a note-on for a silent track is skipped;
- a note-off always goes through.

Mute and solo belong to the open score, so they reset when another score
opens. Play the score and the volume are saved like the metronome.

### 6. The Mix popup

A **Mix** button on the transport opens a call-out with:
- the **Play the score** switch and the volume;
- a row per track with **Mute** and **Solo**. A two-staff part's rows read
  *Piano, right hand* and *Piano, left hand*, which is #201's staff-is-hand
  assumption. A one-staff part's row is its name.

When playback is unavailable, the popup says why instead.

### 7. During a take

The mix plays on. A singer needs the accompaniment, and a pianist can practise
one hand against the other on a take too. The microphone hears the speakers.
#38's calibration and the onset detector's thresholds are where that is
handled. Headphones avoid it, and the wiki says so.

## Risks / Trade-offs

- **CPU:** 96 voices with interpolation at 48 kHz cost a few percent of a
  core. A dense score that runs out of voices takes released notes first.
- **Installer size** grows by about 32 MB on every platform.
- **Downloads:** CI downloads about 32 MB once per clean configure.
- **Sample provenance** in GeneralUser GS is the author's caveat. It is
  surfaced for the release review rather than resolved here.
  *Accepted by the maintainer for 0.7.0 (2026-11-08).* The licence (v2.0)
  allows the bank in software projects and lets its packaging change, with no
  attribution, share-alike or non-commercial term; its samples allow full use
  in music production. Every package installs `GeneralUser-GS-LICENSE.txt` and
  `TinySoundFont-LICENSE.txt` beside the bank, and the build fetches from the
  author's GitHub repository by commit, never his download links, as the
  licence asks.

## Decided while building

- **The strip narrowed by a few pixels to make room for Mix.** Record, Play,
  Stop, the readout, Click and Loop each lost 2–12 px, so the whole strip still
  fits beside the menus in a 1280-pixel window: 576 px full, 414 px compact.
- **Mute and Solo are coloured when on,** amber and green, because the
  look-and-feel's toggled shade was too close to the off one to read in either
  theme.
- **The popup opens inside the window** (`CallOutBox` with the top-level window
  as parent), so a capture sees it. Like any call-out it closes when the window
  is resized, which is why the harness's tiny-window picture shows the strip
  without it.
- **Each track's name** is computed by the clock alongside its tracks
  (`playbackTrackNames`), so the transport strip needs no score of its own.
- **Allocation-free on the audio paths, checked locally** as well as by
  RealtimeSanitizer in CI. A build of `tsf.h` with counting
  `TSF_MALLOC`/`TSF_REALLOC` ran 50 rounds of the renderer's calls after
  loading: program changes on 64 channels, 200 note-ons against 96 voices (so
  voices are stolen), note-offs, releases, every channel silenced, and sample
  rate changes. It made **zero** allocations. `tsf_reset` frees the channels,
  so the synthesiser silences each channel instead.
