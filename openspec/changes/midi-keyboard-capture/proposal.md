## Why

**Nothing in this repository reads a MIDI input device.** `src/platform/` holds
`audio/` and `score/` and nothing else; `MidiInput` and `MidiMessage` appear
nowhere under `src/`. #34 and #36 are MIDI *file* import and playback — output,
not input.

The 2026-10-05 roadmap refocus made a digital piano the primary instrument, so
this is the foundation the piano work stands on, and #13 names it the stage's
long pole: *"#199 is the long pole and everything in Stage 4 depends on it, so
its OpenSpec proposal should be written early even if the implementation
waits."* This is that proposal.

What live MIDI buys is not a feature, it is the removal of a research problem:

- With exact note on/off, velocity and pedal, most of Stage 4's evaluation
  becomes bookkeeping rather than DSP. #42 (pitch matching), #43 (onset timing),
  #44 (duration and sustain), #45 (dynamics) and #48 (missed, extra and wrong
  notes) stop needing a polyphonic transcriber in front of them. #204 —
  polyphonic pitch detection for acoustic piano — is explicitly deferred for
  exactly this reason.
- #200's grand staff has something to *show*: a note the user actually played,
  against a note the score asked for.

And it is the right time rather than merely a possible time. #187 has just
demonstrated what a `src/platform/` subsystem with no application consumer
costs: thirty-five files of score model whose assumptions went unchallenged
until something consumed them, and a findings document of what that revealed.
This change must not repeat it, which is why it ships a consumer — see
*What Changes*.

## What Changes

- Add **`src/platform/midi/`**, a peer of `src/platform/audio/` rather than a
  passenger on it.
- **`MidiInputService`** — owns the application's MIDI input device, mirroring
  `AudioInputService`: one driver callback, one preallocated SPSC FIFO per
  consumer, every consumer draining on a message-thread timer. No consumer code
  runs in the MIDI callback.
- **A JUCE-free translation layer** that turns raw MIDI bytes into the
  application's own event type. Everything worth asserting about the stream —
  note-on with velocity zero being a note-off, CC64's threshold, running status,
  a truncated message — is unit tested from byte arrays, with **no device and no
  JUCE**.
- **Device selection that survives a restart**, through the existing settings
  layer: one new key holding the chosen device's identifier, and the name beside
  it so a device that is no longer present can still be named in the interface.
- **Hot-plug handling** in the shape `AudioRecoveryPolicy` already established:
  a JUCE-free policy deciding *whether* to reopen, a timer deciding *when* to
  ask, and the service doing it. A keyboard unplugged mid-session is reported
  and recovered from, and does not take the application down.
- **A MIDI panel in the settings window** — the device list, the connection
  state, and a live readout of the last events received. This is the consumer,
  and it is deliberately the smallest one that proves the whole path: a user
  whose keyboard is not working looks here, and nothing else in the application
  can yet tell them.
- **Extend the audio-thread-safety contract** to say that the MIDI callback is
  bound by it too, and what is and is not enforced there.

Deliberately **not** in scope:

- **Pedal-aware note duration (#203).** This delivers pedal *events*; deciding
  that a note released under the pedal is still sounding is a model on top of
  them and is its own issue.
- **Clock reconciliation with the audio callback (#38).** Timestamps are
  delivered as the device gave them, plus one message-thread reference reading,
  and the design says exactly what a consumer may and may not conclude from
  them. Calibrating two clocks against each other is #38's, needs hardware to
  validate, and would be guesswork here.
- **Evaluation of any kind** (#42–#48). Nothing here compares a played note to
  anything.
- **Showing captured notes on a staff** (#200), or against the current target
  note. The Target Note tool stays as it is.
- **MIDI output, MIDI file import, and playback** (#34, #36).
- **Anything beyond note on/off and CC64**: no aftertouch, program change,
  pitch bend, other controllers, sysex, clock, or MPE.
- **Multiple simultaneous MIDI devices.** One selected input, like one selected
  microphone.

## Capabilities

### New Capabilities

- **`midi-keyboard-capture`** — a connected MIDI keyboard is selected,
  remembered, and read live, and its note and pedal events reach the
  application on the message thread without the driver callback allocating,
  locking, or blocking.

### Modified Capabilities

- **`runtime-safety-verification`** — the real-time contract now has a second
  driver callback under it. What RealtimeSanitizer actually covers there is
  stated rather than implied, the way the audio callback's coverage already is.
- **`settings-transfer`** — one new persisted key. Additive: a settings file
  written before this change loads with no MIDI device selected.

## Impact

- **New `src/platform/midi/`** — the first platform subsystem added since
  `score/`. Four headers and two sources, of which only the service needs JUCE.
- **`src/application/shell`** — the shell gains ownership of one more long-lived
  service, declared before `liveTools` for the reason the others are.
- **Settings** — a new schema key and a schema version bump. Old files load as
  `migrated` with the field empty, which is the existing behaviour for a key
  that was not there.
- **Settings window** — a fifth panel. The existing four are the pattern.
- **`juce_audio_devices`** is already linked to both targets, so no new JUCE
  module and no new dependency: `MidiInput` lives in the module
  `AudioInputService` already uses.
- **No change to the audio path.** Not one line of `AudioInputService`, and no
  new work on the audio thread.
- **Threading** — a second driver callback thread in the application, the first
  since the audio callback. The contract document gains it.
- **Downstream** — #200, #201, #203, and #42–#48 all read this service; #38
  reconciles its clock with the audio one; Stage 4 depends on it wholesale.
