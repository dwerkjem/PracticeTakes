## Context

The application captures audio and never plays any. `AudioInputService` owns
the only device callback. It clears whatever output it is handed, reads the
input, or renders the synthetic tone, and copies samples into one FIFO per
sample listener. The device is opened as `initialise(2, 0, …)`: two inputs and
no outputs. The callback is `noexcept PRACTICE_TAKES_NONBLOCKING`, and a
RealtimeSanitizer job fails any allocation, lock or blocking call on the paths
its tests drive.

The score model has a `TempoMap`: ticks to seconds and back, exact across tempo
changes, 3,840 ticks per quarter note. It says plainly that it isn't real-time
safe, and that playback must flatten what it needs into preallocated data on
the message thread. `TimeSignature` carries beats per bar and beat type.

Four MVP pieces wait on a clock:
- the score cursor (#33);
- a take (#40), and its timing judgement (#43);
- accompaniment (#286);
- the reference tone and drone (#299), which needs an output path.

## Goals / Non-Goals

**Goals:**

- One position that the microphone, the MIDI keyboard and the sound coming out
  all agree on.
- Count-in and metronome locked to that position at every tempo scale.
- Loops that wrap without drift, however long they run.
- The first output path, inside the existing audio-thread contract and verified
  the same way.
- Every timing rule testable without a device or a display.

**Non-Goals:**

- Playing the score's notes (#286).
- Recording takes (#40).
- Latency measurement and compensation (#38). The clock reports the device's
  stated latencies; #38 measures and applies the real ones.
- More than one loop, or nested repeats. Repeats and jumps in the score are
  the score model's concern. The clock plays the timeline it is given.

## Decisions

### The clock is counted in frames of the callback that captures input

The position is an integer frame count advanced by the audio callback, the
same callback that pushes microphone samples into the FIFOs. A sample captured
at frame *n* and the clock's position at frame *n* are then the same instant,
by construction. That is the property #43 (onset timing) and #38 (latency)
need. The click rendered into the output buffer of that same callback leaves
at frame *n* plus the device's output latency, which #38 measures.

The MIDI keyboard arrives on its own thread with its own timestamps.
`MidiInputService` already stamps events with `juce::Time::getMillisecondCounterHiRes`.
The clock publishes, at each callback, the pair (frame position, high-resolution
time), so a MIDI timestamp converts to a frame position by interpolation. That
conversion is #40's to use; the clock only has to publish the pair.

*Alternatives rejected:*
- A message-thread timer as the clock: it jitters by tens of milliseconds and
  shares no timeline with the input.
- A separate output `AudioDeviceManager`: two devices, two sample clocks, and
  a drift between them that no tempo is safe from.

### Duplex on the device AudioInputService already opens

`initialise(2, 2, …)` instead of `(2, 0, …)`. The output device is chosen in
**Settings → Audio**, beside the input. JUCE's device types give one device
object a separate input and output name where the platform allows it (ALSA,
CoreAudio aggregate, WASAPI), so "microphone A, headphones B" works without a
second callback.

When there's no output device, or it fails to open:
- the input keeps working, exactly as today;
- the clock keeps running from the input callback;
- the metronome is silent;
- Settings says why.

With no device at all (a declined capture instance, or no hardware), the
existing `ToneSource` timer already produces blocks. It calls the renderer with
no output, so the clock runs in verification captures too.

### One real-time renderer, hosted by the callback, published atomically

`AudioInputService` gains `setRealtimeRenderer(RealtimeRenderer*)`. The
pointer is published with release and read with acquire. Swapping or removing
it waits for `callbacksInProgress`, the same pattern `removeListener` uses, so
a renderer is never destroyed mid-callback.

The interface is one function:

```cpp
virtual void render(RenderBlock& block) noexcept PRACTICE_TAKES_NONBLOCKING = 0;
```

`RenderBlock` carries the input pointer (after gain), the output channels, the
frame count, the sample rate and the callback's high-resolution time.

The callback calls it after clearing the output and before the mute check.
Mute silences the *microphone*, not the metronome: you can mute your input and
still hear the click.

One renderer, not a list. The clock is the only thing that renders. When
accompaniment arrives (#286), the clock calls it, so the timeline has one
owner. A list would invite a second clock.

### What the audio thread reads: a segment table, not the score

The message thread flattens the `TempoMap` and the time signatures into at
most 512 `ClockSegment` PODs. Each holds:
- the start frame at the current tempo scale;
- the start beat;
- frames per beat;
- beats per bar.

The renderer finds the segment containing a frame by binary search, and the
beat within it by one multiply. A score with more than 512 tempo or meter
changes, which isn't a real score, keeps the first 512 and reports it.

Publishing uses two preallocated tables and an atomic index. The message
thread fills the inactive table, then flips the index with release. The
renderer reads the index once per callback with acquire. The message thread
won't refill a table until a callback has acknowledged the flip (an atomic
generation echo), so a callback never reads a half-written table.

A tempo-scale change is a republish. The current *score* position (in ticks)
is converted back to frames under the new scale, so the music continues from
the same beat rather than jumping.

### Loops wrap by an exact frame count

A loop is set in score ticks. Its start and end are converted to frames
**once**, when it is set or when the tempo scale changes:
`round(seconds(tick) / scale × sampleRate)`. When the position reaches the
end frame, the renderer subtracts `end - start`, an exact integer, and raises a
wrap flag for the message thread.

No floating-point position is ever accumulated, so the thousandth repetition
starts on the same frame as the first. A test pins this across 10,000 wraps at
awkward rates (44.1 kHz, 7/8, a tempo change inside the loop).

A count-in before each loop repetition is optional, off by default. Practising
a passage on repeat usually wants continuity.

### Count-in is negative position

Starting with a count-in sets the position to minus one bar (or *n* bars) at
the tempo and meter of the start point. The clock runs from there: clicks
sound, listeners see `countingIn = true` and a negative position, and at zero
the take begins. No separate state machine is needed for "counting in", and
#40 can begin recording at the count-in, so the first note is never clipped.

### The click is synthesised once, at construction

There are two short clicks, accented (downbeat) and normal: a 1 kHz and
1.5 kHz decaying sine, 30 ms each, rendered into preallocated buffers when the
clock is built. There's no file I/O and no allocation in the callback. A click
that straddles a block boundary carries its read position across blocks. The
volume is an atomic gain.

### Notifications: a display-rate timer, discrete events latched

A 60 Hz message-thread timer reads the atomic frame position and converts it to
ticks and seconds through the **message thread's** `TempoMap`. It then notifies
`ClockListener`s with a `ClockPosition`:
- ticks;
- score seconds;
- the frame;
- whether it is counting in;
- the state;
- the tempo scale.

Wraps, seeks and the end of the piece are latched by the renderer as atomic
flags, and delivered once each, as events, on the next tick. The position
between two ticks is never interpolated or guessed: a listener that needs
sub-tick accuracy (#43) asks for the frame/time pair instead.

### Testability

`ClockMath.h` is JUCE-free and holds every rule:
- segment construction;
- frame to beat;
- beat to whether to click, and which click;
- the loop wrap;
- the count-in start;
- re-anchoring on a tempo-scale change.

The renderer is a thin loop over `ClockMath`. It is driven in tests through
`AudioInputServiceCallbackAccess`, as the callback already is, so
RealtimeSanitizer sees it run. The `PracticeClock` message-thread class is
tested headless with a fake service that delivers blocks.

## Risks / Trade-offs

- **Duplex can fail where input-only worked.** Some machines can't open an
  output with the chosen input. → Mitigation: if opening with outputs fails,
  retry input-only, as today, and show "Metronome unavailable: no output
  device" in Settings. The capture path never regresses.
- **Output latency makes the click late against the clock.** → Mitigation:
  the clock exposes the device's stated output latency, #38 measures the real
  one, and judgements subtract it. The click is never shifted early, because
  that needs lookahead the callback doesn't have.
- **A second renderer will be wanted (#286, #299).** → Mitigation: the clock
  is the renderer, and owns the others. That keeps one timeline. It is
  recorded here so the next change doesn't add a list.
- **The callback does more work.** → Mitigation: one binary search over at
  most 512 segments, and a block-length copy of the click. It is bounded,
  measured by a `[.benchmark]` case, and checked by RealtimeSanitizer.

## Migration Plan

No data migration. A new setting (the output device) defaults to the system
default output. A `.ptsettings` file without it imports unchanged, and the
setting is not exported, because it belongs to the machine, like the MIDI
keyboard.

## Decisions on the open questions

The maintainer settled these on 2026-11-08, accepting each proposed default
except the tempo scale's lower bound:

1. **Where the transport controls live.** A compact strip in the main window's
   title bar (play/pause, stop, tempo %, metronome on/off, loop on/off), shown
   once a score is open.
2. **Count-in length.** One bar by default, 0–4 bars in Settings → Practice.
3. **Tempo scale range and step.** 5%–200%, in 5% steps. The lower bound is 5%,
   not the proposed 25%, so a passage can be worked through very slowly. At 5%,
   a beat of a 60 bpm score lasts 20 s, 882,000 frames at 44.1 kHz. That still
   fits a 64-bit frame position many times over, and a 30 ms click is
   unaffected because its length doesn't scale.
4. **Metronome subdivisions.** Not in this change; beats only, with an accented
   downbeat.

## Implementation notes

Decisions made while building, which the sections above did not settle.

- **A beat is a note of the time signature's beat type.** 7/8 clicks seven
  eighths, 6/8 six. Grouping compound meters into dotted beats is a
  subdivision question, which decision 4 leaves out of this change.
- **Bars follow the measures, not only the time signature.** The importer
  widens a bar whose notes overrun its signature, so a segment's bar length is
  the measure's nominal duration. A widened bar's last part-beat still clicks.
- **A segment starts at every tempo entry and at every change of bar grid.** A
  tempo change in the middle of a bar starts a segment whose bar origin is that
  bar's start, so the beat count carries on.
- **A segment's start frame comes from the tempo map's exact seconds**, never
  from summing earlier segments, so rounding can't accumulate across them.
- **The clicks travel with the table.** Each of the renderer's two table slots
  carries its own pair of clicks, synthesised on the message thread at that
  table's sample rate. A 1.5 kHz click at 44.1 kHz would otherwise play at
  1.63 kHz on a 48 kHz device. "Synthesised at construction" still holds: the
  constructor synthesises both slots, and the audio thread never synthesises
  or allocates.
- **The count-in always sounds**, even with the metronome off. It is how a
  person knows when to start; the metronome switch governs the take.
- **Pausing during a count-in abandons it.** The position returns to the point
  it was counting in to, and playing again counts in afresh.
- **Reaching the end of the piece stops the clock**, as the stop command would
  (back to the loop's start, or the score's), and is reported once.
- **The position during a count-in is the target's frame minus what is left.**
  It is negative when counting in to the start of the score, as the spec
  says. Counting in to bar 9, it runs up to bar 9's frame instead.
- **Commands travel in a 64-entry SPSC queue**, drained at the top of each
  block. A command is several values (a seek's tick, a play's count-in), and
  two in one display tick must both happen in order, which separate atomics
  can't promise.
- **The synthetic-input generator now runs whenever a renderer is set and no
  device is delivering**, tone or no tone, so the clock keeps time with no
  device at all. Running only for the clock, it is not reported as input.
- **The block carries a monotonic stream frame** beside the clock's position
  and the high-resolution time, so #40 can place a MIDI event even while the
  clock is stopped.
- **Cost:** a 512-frame block against a full 512-segment table renders in
  about 0.74 µs on the development machine (`[.benchmark]`, "rendering a block
  against a full segment table").
