Settled inputs, from `design.md` § Decisions:

| | |
|---|---|
| Location | `src/platform/midi/`, a peer of `src/platform/audio/` |
| Device ownership | `juce::MidiInput` held directly, **not** through `AudioDeviceManager` |
| Event type | A trivially copyable POD; no owning members, so the callback can write one without allocating |
| Translation | From raw bytes, JUCE-free, unit tested from byte arrays |
| Overflow | Drop the **oldest**, opposite to `AudioSampleFifo`, plus a lost-event counter |
| Timestamps | Device timestamp and a received reading; no audio-clock relationship asserted |
| Recovery | `MidiRecoveryPolicy`, JUCE-free, mirroring `AudioRecoveryPolicy` |
| Persistence | `midi.inputDeviceIdentifier` + `midi.inputDeviceName`; schema 5 → 6 |
| Consumer | A MIDI panel in the settings window |

## 1. The event, the translation, and the buffer

JUCE-free, no device, no consumer — so each is finished and asserted before
anything needs a keyboard.

- [x] 1.1 Add `src/platform/midi/MidiEvent.h`: the event kind, channel, note
      number, velocity, pedal state, and the two timestamps. `static_assert` it
      is trivially copyable — that is the property the callback depends on, so
      it should fail at compile time rather than become untrue quietly.
- [x] 1.2 Add `src/platform/midi/MidiMessageTranslation.h`/`.cpp` taking
      `(const std::uint8_t* data, int size, double deviceTimestamp, double
      receivedTimestamp)` and returning `std::optional<MidiEvent>`. No JUCE
      include anywhere in either file.
- [x] 1.3 Translate note-on and note-off, carrying velocity and channel.
- [x] 1.4 Translate **note-on with velocity 0 as a note-off**. The single most
      common source of stuck notes in a MIDI consumer; it is a translation rule,
      so it lives here and not in each consumer.
- [x] 1.5 Translate CC64 as the sustain pedal, **down at or above 64**, not only
      at 127 — a half-pedalling keyboard sends intermediate values.
- [x] 1.6 Return nothing for every other message, and for a message shorter than
      its status byte requires. Malformed input from a device is malformed
      input, not a programming error — the same rule `score/musicxml` applies to
      files.
- [x] 1.7 Add `src/platform/midi/MidiEventFifo.h`: fixed capacity, SPSC,
      dropping the **oldest** on overflow, with a monotonic lost-event counter.
      Its header states why it differs from `AudioSampleFifo`, and
      `AudioSampleFifo`'s gains a line pointing at it — two FIFOs with opposite
      policies is a trap for someone reading quickly.
- [x] 1.8 Add `src/tests/platform/midi/MidiMessageTranslationTests.cpp`: note on
      and off, zero-velocity note-on, every channel, velocity bounds, the pedal
      threshold at 63/64/65/127, an ignored message, a truncated message, a
      zero-length buffer, and a null pointer.
- [x] 1.9 Add `src/tests/platform/midi/MidiEventFifoTests.cpp`: ordering, fill to
      capacity, overflow dropping the oldest, the lost counter moving by the
      right amount, drain-to-empty, and a counter that does not reset when read.
- [x] 1.10 Add every new path to `tools/cmake/Sources.cmake`, on both lists.

## 2. The recovery policy

- [x] 2.1 Add `src/platform/midi/MidiRecoveryPolicy.h`: `constexpr`, JUCE-free,
      deciding whether a reopen should be attempted from whether a device is
      selected, whether it is open, and whether it is currently enumerated.
- [x] 2.2 Add `src/tests/platform/midi/MidiRecoveryPolicyTests.cpp` covering the
      whole truth table, as `AudioRecoveryPolicyTests` does.

## 3. The service

- [x] 3.1 Add `src/platform/midi/MidiInputService.h`/`.cpp`. Modular JUCE
      includes, never `JuceHeader.h` — that umbrella is generated for the
      application target only, and using it is the mechanical reason
      `AudioInputService` had no tests for so long.
- [x] 3.2 Enumerate devices, open the selected one, and expose a connection
      state enum (no device selected / selected but absent / open).
- [x] 3.3 Implement `handleIncomingMidiMessage`, annotated
      `noexcept PRACTICE_TAKES_NONBLOCKING` **from its first commit** rather than
      added later. Read `getRawData()`/`getRawDataSize()` and never copy the
      `juce::MidiMessage`: it owns a heap allocation for anything over four
      bytes, so copying one in the callback is an allocation.
- [x] 3.4 Fan each translated event into one preallocated `MidiEventFifo` per
      consumer, under the fixed-size consumer-slot arrangement
      `AudioInputService` uses. No consumer code runs in the callback.
- [x] 3.5 Add the consumer registration, drain, available-count and lost-count
      surface a consumer needs from the message thread.
- [x] 3.6 Scan for device changes on a timer — more often while disconnected
      than while connected, as `AudioInputService` already does — and reopen
      through `MidiRecoveryPolicy`. Keep the open behind a single seam so that
      `AudioInputService::startDeviceWork` can be applied if an open is ever
      observed to block.
- [x] 3.7 Add `src/tests/support/MidiInputServiceCallbackAccess.h` and the friend
      declaration that admits it, following
      `AudioInputServiceCallbackAccess` exactly. Without a test that drives the
      callback, the non-blocking annotation proves nothing — the audio-thread
      document says so in as many words, and repeating that mistake on a second
      callback would be choosing it.
- [x] 3.8 Add `src/tests/platform/midi/MidiInputServiceTests.cpp` driving the
      callback through that seam: events reach a registered consumer in order,
      two consumers each get everything, a consumer that stops draining does not
      starve another, and an unregistered consumer is not written to.
- [x] 3.9 Driven, not confirmed green. 3.8 drives the callback through the
      harness, which is the half that was in this change's gift — an annotation
      on a function nothing executes is what the audio-thread document says
      proves nothing. Whether the RealtimeSanitizer leg actually flags the new
      callback cannot be checked here: it needs Clang 20, which this machine
      does not have and `install-clang-20.sh` declines to add
      non-interactively. CI's Realtime job is where that is established.

## 4. Persistence

- [x] 4.1 Add `midiInputDeviceIdentifier` and `midiInputDeviceName` to
      `AppSettings::State`, with the keys `midi.inputDeviceIdentifier` and
      `midi.inputDeviceName`.
- [x] 4.2 Bump `AppDefaults::schemaVersion` 5 → 6.
- [x] 4.3 Extend `SettingsPersistenceTests`: both keys round-trip, a file
      written at schema 5 loads as `migrated` with no device selected, and the
      keys are in the owned-key list so a reset clears them.
- [x] 4.4 Check whether the settings transfer codec needs the keys listed
      explicitly, and extend `SettingsTransferCodecTests` if it does. A device
      identifier is machine-specific, so decide deliberately whether it belongs
      in an exported settings file at all — and record the answer rather than
      letting the default decide it.

## 5. The shell

- [x] 5.1 Give `MainComponent` the service, declared **before** `liveTools` for
      the same reason `audioInputService` and `scoreImport` are: reverse-order
      member destruction is the whole of how a borrower cannot outlive what it
      borrows, and there is no runtime check.
- [x] 5.2 Apply the persisted device at startup and store the selection when it
      changes.
- [x] 5.3 Do **not** add it to `ToolServices` in this change. Nothing in
      `src/features` reads MIDI yet, and #187 established what the field costs
      to add later: one line at each of two construction sites and no factory
      signature. Adding it before a tool needs it would be guessing at the shape.

## 6. The consumer

- [x] 6.1 Add a MIDI panel to the settings window, following the four existing
      `CategoryPanel` subclasses.
- [x] 6.2 Show the device list and the current selection, the connection state in
      words, and the remembered device by name when it is not connected.
- [x] 6.3 Show a live readout of the most recent events with note names and
      velocities, drained on the panel's own timer. This is the proof the whole
      path works, and the place a user with a non-working keyboard will look.
- [x] 6.4 Show the note **number** and velocity, not a note name. A MIDI number
      cannot be spelled: it cannot distinguish F-sharp from G-flat. Restated
      after merging `main`, which landed `score::spelledName` from #187 — that
      is the right function and it still does not apply here, because it takes a
      `score::Pitch`, which is a spelling. Only the tuner's private
      `noteNameForMidi` turns a bare number into a name, by picking a spelling
      arbitrarily. A played note gets a name once something knows what it was
      *meant* to be, which is #42's matching rather than naming in isolation.
      The panel says so where someone would otherwise add a copy.

## 7. Verification

- [x] 7.1 `ctest --test-dir build --output-on-failure` green, including every
      pre-existing suite.
- [x] 7.2 `run_clang_format.py` and `run_clang_tidy.py` clean over the new and
      changed files.
- [x] 7.3 `check_test_layout.py` — `src/tests/platform/midi/` mirrors
      `src/platform/midi/`, so nothing needs an exemption.
- [x] 7.4 `python tools/scripts/run_tests.py` for any `tools/scripts` change.
- [ ] 7.5 **NOT DONE — needs hardware this machine does not have.** The
      container has no MIDI sequencer at all (`/dev/snd/seq` does not exist), so
      no device can be enumerated, opened or played, and no virtual one can be
      created without kernel module access. Everything below the device open is
      covered by 7.1; the open itself, the real driver callback, the unplug, the
      replug, and the selection surviving a restart against real hardware are
      covered by nothing. Someone with a keyboard must run this before the
      change is believed. Done without hardware instead: the panel's empty
      state, driven through the test-control channel and photographed.

## 8. Documentation

- [x] 8.1 Extend `docs/development/performance/audio-thread-safety.md`: the MIDI
      callback is under the same contract, what RealtimeSanitizer actually
      covers there, and what it does not — stated as precisely as the audio
      callback's coverage already is, rather than implied by the annotation.
- [x] 8.2 Add `src/platform/midi` to the layering section of
      `docs/development/agents/AGENT_GUIDE.md`, and to the architecture
      document.
- [ ] 8.3 **Blocked on 7.5.** Nothing to record until a real keyboard has been
      played into this. The answer is not "nothing was revealed" — it is that
      nobody has looked yet.
- [x] 8.4 Answer `design.md`'s open questions after implementation, as
      `score-import-command` and `score-target-note-tool` did.
