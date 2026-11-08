## 1. Inputs

- [x] 1.1 `Part::midiProgram` from `<midi-program>`, the subset document, and
      tests.
- [x] 1.2 Fetch TinySoundFont and GeneralUser GS at pinned commits; copy the
      SoundFont beside the built executable; install rules for every platform;
      `tools/packaging/sounds/README.md` with both licences.

## 2. Playback, JUCE-free

- [x] 2.1 `playbackEventsOf` (decision 3). Tests.
- [x] 2.2 `PlaybackMix` and its mask (decision 5). Tests.
- [x] 2.3 `ScoreSynth`: load on a worker, channels and voices up front, render
      bounded. Tests against the real SoundFont.
- [ ] 2.4 `ClockRenderer`: events in the slot, dispatch, the mask, releases on
      every discontinuity, the score's volume. Tests, and the RealtimeSanitizer
      path playing notes.

## 3. The application

- [ ] 3.1 `PracticeClock`: build events with the table, load the SoundFont,
      publish the mix; the shell finds the file and saves the switch and
      volume.
- [ ] 3.2 The Mix button and popup (decision 6).

## 4. Verification and docs

- [ ] 4.1 An approved state with the Mix popup open over the sample score, and
      its surface. Read the pictures.
- [ ] 4.2 The wiki (Getting Started: playing the score, hands separately; the
      headphones note), Features rows, README, ARCHITECTURE.md § Practice
      clock, and the agent guide.
- [ ] 4.3 A `status:ready-for-human` issue: listen to it on real speakers and
      headphones, with a take.
