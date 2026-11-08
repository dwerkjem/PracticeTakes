## Context

`AudioInputService` runs one callback. It clears the output, runs the practice
clock's `ClockRenderer`, then pushes the input into a 65,536-sample FIFO per
tool. The renderer keeps the clock's position in frames of that same callback,
and publishes a snapshot pairing the position with the block's
high-resolution time. `MidiInputService` stamps events on the same
high-resolution counter and hands them to each consumer through its own FIFO.
`PracticeClock` (message thread) owns the renderer, sends it commands, and
notifies listeners at 60 Hz.

## Goals / Non-Goals

**Goals:**
- A take whose audio, keyboard events and score position share one timeline,
  exact for audio and within the callback's timing jitter for MIDI.
- A lifecycle with every end named, and no silent truncation.
- Nothing allocated, locked or blocked on the audio thread.
- The lifecycle, the assembly and the take's arithmetic tested without a device.

**Non-Goals:** analysing, displaying or saving a take (see the proposal).

## Decisions

### 1. The renderer records the input, stamped per block

While a take records, `ClockRenderer::render` copies the block's input into a
preallocated SPSC sample FIFO of 262,144 samples (about 5.4 s at 48 kHz). It
also pushes one `RecordedBlock` stamp into a 4,096-entry SPSC queue. Each stamp
holds:
- the clock position of the block's first frame;
- how many frames were copied;
- whether the clock was counting in.

The message thread drains both at 60 Hz.

The clock's position is advanced in the same function, so a recorded sample's
position is exact. No inference from a consumer FIFO, which carries no
positions, is needed. A block whose samples don't fit is not pushed, and
neither is its stamp. The overflow is counted, and the take ends as failed
rather than recording a gap that later timing would misread.

The renderer is handed **the input the tools are given**:
- the synthetic tone when one is asked for, otherwise the device's first
  channel;
- after gain;
- nothing while muted.

So a take hears exactly what the analysis tools hear, and the capture harness
can record one. The callback is reordered for this: the input is chosen, the
tone rendered, then the renderer called, then the samples delivered. The tone
is still not rendered while muted, and the renderer still runs while muted,
so the metronome is unaffected.

*Rejected:*
- **A FIFO consumer slot for the take.** Its samples carry no frame position,
  and a dropped block would silently shift everything after it.
- **Recording on the message thread from SharedPitchAnalysis.** Its
  observations are already analysed and decimated, and onset timing (#43)
  needs the waveform.

### 2. Keyboard events through the (frame, time) pair

`TakeRecorder` is a `MidiInputService` consumer. Each event's device timestamp
(the JUCE counter, in seconds) becomes a clock frame through the
`ClockTiming` the renderer published. That frame is the block's start, plus
the elapsed time times the sample rate. The callback's scheduling jitter (one
block) bounds the error; #38 measures and corrects the rest. A key event before
the count-in starts or after the take ends is not part of the take.

### 3. The lifecycle

`TakeLifecycle` (JUCE-free) is the state machine. The live states are ready,
countingIn, performing and paused. A take ends in one of four:

- **completed**: the clock reached the end of the piece.
- **stoppedEarly**: Record or Stop before the end. It is kept, with the bar it
  stopped in. This is the "incomplete" #40 asks to tell apart.
- **cancelled**: discarded by the reader. Nothing is kept.
- **failed**: a reason is kept, and so is what was recorded. The causes are:
  - another score was opened;
  - every source was lost (no microphone and the keyboard gone);
  - the recording overflowed.

Transitions follow the clock. Counting in becomes performing at position zero
relative to the start point, a pause pauses, playing resumes, and the end of
the piece completes. Any other request is ignored, and the lifecycle tests
check exactly which transitions are allowed.

### 4. The clock is locked during a take

`PracticeClock::lock()` makes `setScalePercent`, `setLoopEnabled`,
`setLoopRegion` and `seek` do nothing, and makes `isLocked()` true, until
`unlock()`. The transport strip and the Score View grey out what can't be
used. Opening another score can't be prevented: it fails the take, as
decision 3 says. The metronome and its volume stay free: changing what you
hear doesn't invalidate what you play.

### 5. What a take keeps

`take::Take` (JUCE-free) holds:
- an id, a state and an end reason;
- the score's title and identity (its pointer, held as `shared_ptr<const Score>`
  so it outlives being replaced);
- the start tick and the tempo scale;
- a copy of the clock's `ClockTable`, for frame-to-tick conversion;
- the sample rate;
- the device's stated input and output latencies in frames;
- the sources used;
- the audio: the first sample's clock position and the samples;
- the keyboard events, each with its clock frame;
- the frames recorded while counting in.

Takes live in the recorder for the session, newest last, at most 50. The
oldest is dropped beyond that, because a take is about 11 MB a minute at 48 kHz
mono float.

### 6. Assembling the timeline

`TakeAssembler` (JUCE-free) appends stamped blocks to a take's audio. A block
continues the previous one when its position is the previous end. Anything
else is a discontinuity, which a locked clock never produces. The assembler
reports it, and the recorder fails the take ("the clock jumped"), so later
analysis can rely on the audio being contiguous.

### 7. Starting, and what can be recorded

Record is enabled when a score is open and at least one source is usable: an
input that isn't muted, or an open keyboard. Pressing it:
1. stops the clock if it is running;
2. locks the clock;
3. arms the renderer;
4. plays with the configured count-in, from the current position.

That is the start of the score, or wherever the cursor was put. A take
started with no count-in begins performing at once. Pressing Record again, or
Stop, ends the take as stoppedEarly, unless it has already reached the end.

### 8. Ownership

`TakeRecorder` is owned by `MainComponent`. It is declared after
`practiceClock` and `midiInputService`, whose services it uses, and before
`transportStrip` and `liveTools`, which use it. `ToolServices` gains `takes`,
so a later tool (#52 review) can list and read takes. Its destructor ends a
live take as stoppedEarly, disarms the renderer and unlocks the clock.

## Risks / Trade-offs

- **Memory.** A take is about 11 MB a minute. Fifty is the cap, which #39 will
  revisit when takes are saved.
- **MIDI jitter is one block.** At 512 frames and 48 kHz that is 10.7 ms,
  below the 20–30 ms that #43 will call "on time". #38 can tighten it.
- **The callback does more work while recording**: one block-length copy and
  one stamp. Both are bounded, and the recording path is driven in a
  `[callback]` case so RealtimeSanitizer sees it run.
