## Context

This is the first new `src/platform/` subsystem since `score/`, and the first
new driver callback since `AudioInputService`. Almost everything it needs has
been built once already, for audio, and the useful question is which of those
decisions transfer and which do not.

What `src/platform/audio/` established, and what this inherits:

- **One owner of the device, many consumers.** `AudioInputService` owns the only
  microphone callback and fans input into one bounded SPSC FIFO per consumer.
  No consumer code runs in the callback.
- **The callback is bounded and annotated.** `noexcept PRACTICE_TAKES_NONBLOCKING`,
  no allocation, no lock, no blocking call, no logging, no UI. See
  `docs/development/performance/audio-thread-safety.md`.
- **The policy is JUCE-free and the plumbing is not.** `AudioRecoveryPolicy` is
  four booleans and a `constexpr` function, unit tested without a device;
  `AudioInputService` is the part that needs JUCE and is tested only where a
  friend declaration lets a harness drive it.
- **A device that will not open must not take the application down.**
  `snd_pcm_prepare` through PipeWire can wait forever on a condition variable
  nobody signals, which is why the audio reopen runs on a thread that is
  allowed to outlive the service.

What is different about MIDI, and drives most of what follows:

- **A MIDI message is an event, not a stream.** The audio FIFO carries an
  unbounded flow of `float` where dropping the newest block is survivable and
  invisible. A dropped note-off is not: it leaves a note held forever in every
  consumer that tracked it. Overflow has to be handled differently.
- **The callback is per-device and not the audio thread.** JUCE calls
  `handleIncomingMidiMessage` on a high-priority thread it owns per input
  device. It is not the audio thread, and nothing it does can underrun a buffer
  — but it is a driver callback, and the same discipline applies for the same
  reason.
- **There is no "format".** No sample rate, no channel count, nothing for a
  consumer to be told about before events start.
- **`juce::MidiMessage` owns a heap allocation for anything over four bytes.**
  Copying one in the callback is an allocation. The callback must therefore read
  the message and never keep it.

Three precedents to copy rather than reinvent:

| Need | Precedent |
|---|---|
| Driver callback fanning into per-consumer FIFOs | `AudioInputService` + `AudioSampleFifo` |
| A JUCE-free policy for "should we try to reopen" | `AudioRecoveryPolicy`, with `AudioInputServiceRecoveryAccess` as the seam that makes it testable |
| A settings key with a schema bump and a default | Every key in `SettingsPersistence.cpp`; `AppDefaults::schemaVersion` |

## Goals / Non-Goals

**Goals**

- A connected MIDI keyboard can be selected, and the selection survives a
  restart.
- Note on, note off, velocity and sustain pedal reach a consumer on the message
  thread, with timestamps.
- A device unplugged mid-session is reported and recovered from without taking
  the application down.
- The event stream is covered by tests that need no MIDI hardware **and no
  JUCE** — not merely tests that happen to pass without a keyboard plugged in.
- Nothing in the MIDI callback allocates, locks, or blocks, and that claim is
  checkable rather than asserted.
- It ships with a consumer. #187's lesson is that infrastructure with no
  consumer is infrastructure whose assumptions are untested.

**Non-Goals**

- Pedal-aware note duration (#203), evaluation (#42–#48), notation (#200),
  transport (#36), MIDI files (#34), MIDI output.
- Clock reconciliation with the audio callback (#38).
- Any MIDI beyond note on/off and CC64.
- More than one simultaneous input device.
- Changing anything in `src/platform/audio/`.

## Decisions

### Own the device directly, not through `AudioDeviceManager`

JUCE offers both: `AudioDeviceManager::setMidiInputDeviceEnabled` plus
`addMidiInputDeviceCallback`, or `juce::MidiInput::openDevice` held directly.
The application already owns exactly one `AudioDeviceManager`, inside
`AudioInputService`, and routing MIDI through it would come free with
persistence, since that manager's state XML already round-trips through
`AppSettings::State::audioDeviceState`.

**Taking the direct route anyway**, for three reasons that outweigh the free
persistence:

1. **It would couple MIDI's liveness to audio-device recovery.** That recovery
   is the one subsystem in this repository documented as able to block without
   limit — `AudioInputService::reopenDevice` exists on a thread of its own
   precisely because `snd_pcm_prepare` can wait forever. A pianist with a MIDI
   keyboard and no microphone is a normal user of this application, and their
   keyboard must not stop delivering because a microphone they do not have is
   being retried.
2. **The persistence shape is wrong.** `AudioDeviceManager`'s state is an opaque
   XML blob that JUCE writes and reads. "Which MIDI device did the user choose"
   is a question the settings layer should be able to answer, test, and show in
   a transfer file — a named identifier, not a substring of someone else's blob.
3. **It keeps `midi/` a peer of `audio/`.** #199 asks for a subsystem mirroring
   `AudioInputService`, and a subsystem that is really a view onto another
   subsystem's device manager is not that.

The cost is honest: device enumeration, selection, and hot-plug are written here
rather than inherited. They are about a hundred lines, and two thirds of that is
the policy, which is JUCE-free and tested.

### The event type is a POD, and the translation to it is JUCE-free

```
namespace midi
{
enum class MidiEventKind { noteOn, noteOff, sustainPedal };

struct MidiEvent
{
    MidiEventKind kind;
    int channel;            // 1-16, as MIDI numbers them
    int noteNumber;         // 0-127; unused for sustainPedal
    int velocity;           // 0-127; release velocity on a note off
    bool pedalDown;         // only meaningful for sustainPedal
    double deviceTimestampSeconds;
    double receivedTimestampSeconds;
};
}
```

Trivially copyable, fixed size, no owning members — which is what lets it go
through a FIFO the callback writes without allocating.

The translation takes **raw bytes**, not a `juce::MidiMessage`:

```
[[nodiscard]] std::optional<MidiEvent> translate(
    const std::uint8_t* data, int size, double deviceTimestamp, double receivedTimestamp);
```

This is the decision that makes the acceptance criterion "covered by tests that
need no MIDI hardware" mean something. Tested from byte arrays, the cases that
actually bite are all reachable without a device or a JUCE object:

- **Note-on with velocity 0 is a note-off.** The single most common source of
  stuck notes in MIDI consumers, emitted by a large fraction of real keyboards
  under running status. It is a *translation* rule, so it belongs here and is
  tested here.
- **CC64 ≥ 64 is pedal down, < 64 is up.** The threshold is in the spec and is
  routinely written as `== 127`, which is wrong for half-pedalling keyboards.
- **Everything else is dropped**, and dropping is not an error.
- **A truncated or malformed message yields nothing** rather than reading past
  the buffer — the file-format lesson from `score/musicxml`, applied to a
  device: input from outside is malformed input, not a programming error.

`juce::MidiMessage` is still what the callback is handed; it is read through
`getRawData()`/`getRawDataSize()` and never copied, which is also what keeps the
callback allocation-free.

### Overflow drops the oldest, and says so

`AudioSampleFifo` drops the **newest** block when full and keeps what is
buffered, which is right for audio: the alternative is a gap in the middle of a
window, and a late block is worth less than a continuous one.

**For events it is the wrong way round, and neither choice is actually safe.**
Dropping the newest loses a note-off and leaves a note held forever; dropping
the oldest loses a note-on and leaves a note-off arriving for a note nobody
started. There is no ordering of MIDI events that is safe to lose.

So: a separate `MidiEventFifo` rather than reusing `AudioSampleFifo`, dropping
the **oldest** on overflow — losing the past rather than the future, so the
consumer's idea of what is held now re-converges within one pedal or key
release — **and a monotonic dropped-event counter the consumer can read.** A
consumer that cares about held-note state checks the counter and resets its
state when it has moved, rather than silently believing a corrupt picture.

A capacity of 1024 events per consumer makes this theoretical rather than
practical: a ten-finger chord with pedal is about two dozen events, and the
consumer drains twenty times a second. Dropping at all means a consumer has
stalled for seconds.

### Timestamps: two numbers, and a documented limit on what they mean

Each event carries what the device said (`juce::MidiMessage::getTimeStamp()`,
which for `MidiInput` is a seconds value on a backend-chosen origin) **and** a
reading of `juce::Time::getMillisecondCounterHiRes()` taken at the top of the
callback.

Both, because neither answers on its own. The device timestamp is the better
relative clock between two MIDI events; the received timestamp is the only one
on an origin anything else in the application shares.

The header states the limit in as many words: **the difference between a MIDI
timestamp and an audio-callback position is not known here and must not be
computed from these fields.** Aligning the two clocks is #38, it needs hardware
and a calibration signal to validate, and a number invented here would be used
by #43's onset timing as though it had been measured.

### Recovery mirrors audio, minus the thread, with a stated reason

`MidiRecoveryPolicy` is JUCE-free and `constexpr`, taking the same shape as
`AudioRecoveryPolicy`: whether a reopen should be attempted, from whether a
device is selected, whether it is open, and whether it is currently enumerated.
A timer scans the device list — infrequently while connected, more often while
not, as `AudioInputService` already does.

`AudioInputService` runs its reopen on a thread that may outlive the service,
because an ALSA reopen can block forever. **This does not**, and the reason is
recorded rather than assumed: `MidiInput::openDevice` opens a sequencer client
rather than preparing a PCM stream, and the failure mode that forced the audio
thread — another holder of the PCM device — has no equivalent for a MIDI input,
which ALSA allows several readers of.

That is a claim, not a proof. If an open is ever observed to block, the fix is
`AudioInputService::startDeviceWork` applied here, which is why the open is a
single call behind one seam rather than inline in the timer.

### Persistence: an identifier and a name

Two new keys rather than one: `midi.inputDeviceIdentifier` and
`midi.inputDeviceName`.

The identifier is what reopens the device. The name is what the interface shows
when it cannot — a user whose keyboard is unplugged should read "Casio
PX-S1100 (not connected)", not an opaque identifier or a blank. This is the same
reason `score/` keeps a part's printed measure number as well as its index.

`AppDefaults::schemaVersion` goes 5 → 6. A settings file written before this
loads with both keys absent, which means no device selected — the same state as
a fresh install, and already a state the service has to handle.

### The consumer is a settings panel

The whole path — device open, callback, FIFO, drain — has to be proved by
something, and #187 is the standing argument for proving it now rather than
later.

The smallest consumer that proves all of it is a **MIDI panel in the settings
window**: the device list, the connection state, and a live readout of the most
recent events with their velocities. It is also the place a user with a
keyboard that is not working will actually look, which no other candidate is.

Rejected alternatives: showing captured notes against the Target Note tool's
target (that is #42's matching, wearing a disguise), and a new MIDI monitor tool
in the catalog (a roster entry for a diagnostic, and #187 just documented what a
new tool really costs).

### Testability

The split follows `AudioRecoveryPolicy`/`AudioInputService` exactly.

**In `PracticeTakesTests`, with no JUCE and no device:** the byte-level
translation, every rule above; the FIFO's ordering, its overflow behaviour and
its dropped counter; the recovery policy's truth table.

**Through a friend-declared harness, as `AudioInputServiceCallbackAccess`
does:** the callback itself, so that the non-blocking annotation is checked by
RealtimeSanitizer against code that actually runs. The audio-thread document is
explicit that an annotation on a function no test drives proves nothing, and
repeating that mistake on a second callback would be choosing it.

**Not tested, and said so:** opening a real device, and the panel.

## Risks / Trade-offs

- **No hardware in CI, and none in the author's test loop by default.** Every
  layer but the device open is covered without it, which is the most this can
  be. The first real keyboard will find something, and the thing most likely to
  be wrong is a translation rule for a dialect no byte-array test anticipated.
  Mitigated by the translation being the one layer that is pure and cheap to
  extend when it is.
- **A second driver callback thread.** The application has had one for its whole
  life. Two means two things to keep bounded and two things a reviewer has to
  remember. The contract document gaining a section is the mitigation, and it is
  a weak one.
- **Dropping the oldest event is a real behaviour change from the audio FIFO**,
  in a repository where "the FIFO drops the newest" is otherwise true. Two
  FIFOs with opposite policies is a trap for someone reading quickly. Both
  headers will say why they differ, and the names differ.
- **Owning the device rather than using `AudioDeviceManager`** means
  re-implementing enumeration and hot-plug, and JUCE's device-change
  notifications for MIDI are less uniform across backends than for audio. If
  the timer scan proves unreliable on a platform, that is where it will show.
- **Guessing that `MidiInput::openDevice` does not block.** Recorded as a claim
  with the fix named, rather than discovered during a freeze.
- **Settings schema bump.** Every settings file in existence becomes `migrated`
  on next load. That is the established path and is already exercised, but it is
  not free.

## Migration Plan

Additive. No persisted format changes meaning, no existing key moves, and a
settings file from before this change loads with no MIDI device selected — which
is the same state as a new install.

The order that keeps the tree building and each step testable:

1. `MidiEvent`, the translation, and `MidiEventFifo`, with their tests. JUCE-free
   and with no consumer; nothing else compiles against them yet.
2. `MidiRecoveryPolicy` with its truth table.
3. `MidiInputService`: enumeration, open, callback, fan-out, the timer, and the
   friend-declared harness access. The callback carries the annotation from its
   first line rather than gaining it later.
4. Settings keys and the schema bump.
5. Shell ownership, declared before `liveTools`.
6. The settings panel.
7. The contract document, the approved state and surface for the panel, and the
   architecture guide.

Rollback at any point before 5 leaves dead but harmless code; after 5 it is
removing one member and one panel.

## Open Questions

- **Should a consumer be told about a device change, or discover it?** Audio has
  a `Listener` interface with `audioInputAboutToStart`/`Stopped`. MIDI has no
  format to announce, so the only events are "a device appeared" and "went
  away", and a consumer polling a state enum on its existing timer may be
  enough. Leaning towards the enum, and the panel is not a demanding enough
  consumer to settle it.
- **Does the dropped-event counter belong per consumer or per service?** Per
  consumer is more accurate and matches the FIFO; per service is what a user
  would be shown. Probably both, and that is probably one too many numbers.
- **Should the panel live in the settings window or beside the microphone
  controls in the Audio panel?** A pianist does not think of their keyboard as
  an audio device, and the Audio panel is already long. Going with its own
  panel, but it is a question about what a user expects rather than about code.
- **What happens to held notes when a device disconnects?** Something must
  decide that every held note ends, and the honest place is the consumer that
  tracked them — but then every consumer has to remember to. #203 will have an
  opinion; this change only has to make the disconnection visible.

## Answers, recorded after implementation (task 8.4)

- **Should a consumer be told about a device change, or discover it?**
  Discover it. `connectionState()` is an enum a consumer reads on the timer it
  already has, and the settings panel needed nothing more. A `Listener`
  interface was not written, and the audio one is not the precedent it looked
  like: audio has a *format* to announce — sample rate, channel count — that a
  consumer must have before it can interpret a single sample. MIDI has none.
  The question comes back if a consumer ever needs to act on the disconnection
  itself rather than on its own schedule, which is the held-note question below.
- **Per-consumer or per-service dropped-event counter?** Per consumer, and one
  too many numbers was indeed the risk — so the service has
  `receivedEventCount()` for "is anything arriving at all", which is what a user
  is shown, and `lostEvents(consumer)` for "is my picture of what is held still
  good", which is what a consumer asks. They answer different questions and
  neither is a worse version of the other.
- **Its own panel, or beside the microphone controls?** Its own. The Audio panel
  is already long enough to need a viewport for the device selector, and a
  pianist does not think of their keyboard as an audio device. Cheap to merge
  later if that turns out to be wrong; much harder to split.
- **What happens to held notes when a device disconnects?** Still unanswered,
  and now sharper: the service reports the disconnection, and nothing ends the
  notes. With one consumer that shows a rolling log it does not matter. The
  first consumer that *tracks* held notes has to decide, and making every such
  consumer remember is the kind of thing that is forgotten once. #203 should
  settle it, most likely by the service synthesising note-offs for everything it
  believes is held when a device goes away.

Three things the implementation settled that this design did not ask:

- **The settings transfer file deliberately does not carry the MIDI
  selection**, and `applyTransferredSettings` deliberately does not clear it.
  Task 4.4 asked the question and this is the answer: a transfer file moves
  settings between machines, and which keyboard is plugged into *this* one does
  not travel. Importing someone else's would at best name a device that is not
  here, and would silently replace a working selection. Noted because the audio
  device state *is* carried, which is a precedent this declines to follow rather
  than an inconsistency nobody noticed.
- **`RealtimeSafety.h` is no longer about audio.** It still lives in
  `src/platform/audio/`, and `src/platform/midi/` includes it across the
  subsystem boundary. Left there rather than moved, because moving it would
  touch `AudioInputService` and this change deliberately touches none of it —
  but it is now misfiled, and the third user should move it rather than add a
  second cross-include.
- **The device-open seam was never exercised.** `openDevice` is one call behind
  one function specifically so `AudioInputService::startDeviceWork` can be
  applied if a MIDI open ever blocks. Nothing here establishes that it does not;
  the reasoning in *Decisions* is a reasoned claim about ALSA sequencer clients,
  and task 7.5 is what would test it. It has not been run.
