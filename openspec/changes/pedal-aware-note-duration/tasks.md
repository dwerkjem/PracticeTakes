Settled inputs, from `design.md` § Decisions:

| | |
|---|---|
| Pedal on the event | `pedalPosition` 0–127; `pedalDown` **removed**; `velocity` unwritten for pedal events |
| Threshold | Applied by the tracker from a handed-in value, never at capture; setting `midi.pedalDownThreshold`, 1–127, default 64, clamped on load |
| Time base | `deviceTimestampSeconds` only; intervals, no audio-clock claim |
| Tracker | JUCE-free, message thread, one slot per (channel, note), pedal position per channel |
| Pedal certainty | Unknown at start and after lost events; unknown-pedal notes are not judged |
| Disconnect / loss | `invalidate(atSeconds)` ends every held note `abandoned` |
| Late pedal | Does **not** extend a note |
| Judgement | Pure function; pedal-held notes are never `long`; tolerance a named constant, not a setting |
| Pedal markings | Reported, never compared; the display says they are not read |

## 1. Name the pedal's position, and move the threshold out of capture

JUCE-free, no consumer yet, so the tree keeps building at every step.

- [ ] 1.1 Add `pedalPosition` to `MidiEvent`; state in its comment that it is
      meaningful only for `sustainPedal`. Remove `pedalDown`. Stop writing
      `velocity` for pedal events and fix that field's comment.
- [ ] 1.2 `MidiMessageTranslation`: set `pedalPosition` from CC64's value; rename
      `sustainPedalDownThreshold` to `defaultPedalDownThreshold` and say in its
      comment that it is a setting's default, not a MIDI fact.
- [ ] 1.3 `grep` every reader of `pedalDown` and of `velocity` on a pedal event
      (the panel readout, tests, `MidiInputServiceTests`, support access
      headers) and move each to the position. Nothing may keep reading a field
      that no longer says what it used to.
- [ ] 1.4 Update `MidiMessageTranslationTests`: the 63/64/65/127 cases now assert
      the **position** is carried unchanged, including 0 and a mid value, and that
      a pedal event's `velocity` is zero.

## 2. The tracker

- [ ] 2.1 Add `src/platform/midi/NoteSoundingTracker.h`/`.cpp` (no JUCE include)
      with `SoundedNote`, `SoundingEnd` (`finger`, `pedal`, `restruck`,
      `abandoned`) and `PedalCertainty` (`known`, `unknown`), plus
      `fingerSeconds()`, `soundingSeconds()`, `heldByPedal()`.
- [ ] 2.2 `consume(const MidiEvent&, std::vector<SoundedNote>& finished)`. Note on
      opens a slot; note off closes it. Pedal up at key up → `finger`; pedal down
      at key up → stays open until the pedal crosses below the threshold, then
      `pedal`.
- [ ] 2.3 Re-strike: note on for a slot that is open (key held) **or** awaiting
      pedal release closes the earlier note as `restruck` at the new strike time.
      Test both variants.
- [ ] 2.4 A pedal pressed after key up must not extend that note. Test it
      explicitly — it is the design's one deliberate departure from physics.
- [ ] 2.5 Per-channel pedal position; test that channel 1's pedal does not
      sustain channel 2's note.
- [ ] 2.6 `setPedalThreshold(int)` clamped to 1–127; applies from the next
      decision. Test the 63/64/65/127 crossings here, under the default, and the
      same stream under a different threshold giving a different result.
- [ ] 2.7 Pedal certainty: unknown before the first pedal event per channel and
      after `invalidate`; a note released while unknown is emitted
      `PedalCertainty::unknown` with `soundingEnd == keyUp`.
- [ ] 2.8 `invalidate(double atSeconds, std::vector<SoundedNote>&)`: every held
      key, and every note awaiting pedal release, ends `abandoned` at
      `atSeconds`; every channel's pedal becomes unknown. Idempotent.
- [ ] 2.9 Malformed streams are inputs, not errors: a note off with no note on, a
      second note on for a held key, a pedal up with no prior down. Each is
      defined and tested, none asserts.
- [ ] 2.10 Add `src/tests/platform/midi/NoteSoundingTrackerTests.cpp` covering the
      spec's scenarios by name: pedal up at release, early release held by the
      pedal (1s + 2s → 3s), pedal after release, re-strike, channel
      independence, unknown pedal, invalidate. Hand-built `MidiEvent` streams,
      no device.

## 3. The judgement

- [ ] 3.1 Add `src/platform/midi/SoundingDurationJudgement.h`:
      `judgeSoundingDuration(notatedSeconds, const SoundedNote&, tolerance)`
      returning `Outcome` (`short`, `onTarget`, `long`, `notJudged`) and `HeldBy`
      (`finger`, `pedal`). `defaultDurationTolerance` is a named constant with a
      comment pointing at #44/#50.
- [ ] 3.2 Rules as in `design.md`: short on sounding time; long only when
      finger-held; `notJudged` for `abandoned`, unknown pedal, or non-positive
      notated duration.
- [ ] 3.3 Add `src/tests/platform/midi/SoundingDurationJudgementTests.cpp`: the four
      acceptance scenarios (released early + pedal → not short; released early,
      no pedal → short; finger-held long → long; pedal-held long → onTarget with
      `HeldBy::pedal`), tolerance edges either side, and each `notJudged` cause.
- [ ] 3.4 Add all new paths to `tools/cmake/Sources.cmake`, both lists.

## 4. Persistence

- [ ] 4.1 Add `midiPedalDownThreshold` to `AppSettings::State`, key
      `midi.pedalDownThreshold`, default `defaultPedalDownThreshold`.
- [ ] 4.2 Bump `AppDefaults::schemaVersion` 6 → 7.
- [ ] 4.3 Extend `SettingsPersistenceTests`: round-trip; a schema-6 file loads
      `migrated` with 64; 0, −5 and 200 clamp to 1, 1 and 127; the key is in the
      owned-key list so a reset clears it.
- [ ] 4.4 Decide, and **record**, whether `SettingsTransferCodec` carries it.
      Lean: yes, a threshold is a preference about a keyboard, not a machine
      identifier. Extend `SettingsTransferCodecTests` to match whichever is chosen.

## 5. The shell and the consumer

- [ ] 5.1 Load and store the threshold in `MainComponentSettings.cpp` alongside the
      device selection; hand it to the panel's tracker.
- [ ] 5.2 In the MIDI panel, own a `NoteSoundingTracker`, feed it from the events the
      panel already drains, and call `invalidate` when the lost-event counter
      advances or the connection state leaves *open*, using the last event's device
      timestamp.
- [ ] 5.3 Add a labelled slider (1–127) with its numeric value shown; changing it
      persists and calls `setPedalThreshold`.
- [ ] 5.4 Extend the readout: per finished note, finger duration, sounding duration
      and a *pedal* tag; abandoned and unknown-pedal notes are marked as not
      measured, not shown as zero.
- [ ] 5.5 State in the panel, in words, that pedal markings in a score are not read.
- [ ] 5.6 Do **not** add the tracker to `ToolServices`. No tool consumes it until
      #42; adding it first would be guessing at the shape (the same reasoning as
      `midi-keyboard-capture` 5.3).

## 6. Verification

- [ ] 6.1 `ctest --test-dir build --output-on-failure` green, including every
      pre-existing suite.
- [ ] 6.2 `run_clang_format.py` and `run_clang_tidy.py` clean over new and changed
      files.
- [ ] 6.3 `check_test_layout.py` — new tests mirror `src/platform/midi/`.
- [ ] 6.4 The panel's empty state and slider captured through the test-control
      channel and looked at.
- [ ] 6.5 **Needs hardware — do not mark done without it.** Play a real keyboard
      with a real pedal: confirm device timestamps advance, record what a
      half-pedal ramp and a chattering pedal actually send, and confirm per-channel
      pedal state matches the notes' channel. Answers the two hardware-dependent
      open questions. Shares its blocker with `midi-keyboard-capture` 7.5.

## 7. Documentation

- [ ] 7.1 Document the sounding-interval model and its two deliberate
      simplifications (late pedal, no decay) in
      `docs/development/architecture/` beside the other platform documents, and
      index it in `docs/development/README.md`.
- [ ] 7.2 Add the new headers to the `src/platform/midi` entry in
      `docs/development/agents/AGENT_GUIDE.md`.
- [ ] 7.3 Answer `midi-keyboard-capture`'s "what happens to held notes on
      disconnect" in its recorded answers, pointing here.
- [ ] 7.4 Answer this change's open questions after implementation, as the previous
      changes did.
