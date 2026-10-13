# Audio-thread safety

`AudioInputService::audioDeviceIOCallbackWithContext` runs on the real-time
audio thread. Everything below is a hard constraint: a violation does not
produce a test failure or a wrong number, it produces an audible dropout in
someone's practice session.

**There are now two driver callbacks under this contract**, not one. The second
is `MidiInputService::handleIncomingMidiMessage`, which JUCE calls on a
high-priority thread it owns per MIDI input device. It is not the audio thread
and cannot underrun an audio buffer, so its failure mode is a stuck or late
note rather than a dropout — but it is a driver callback, the discipline is the
same, and it carries the same annotation. See
[The MIDI callback](#the-midi-callback) below for what differs.

## The contract

The callback may:

- clear the output buffers;
- read atomics (mute, gain, sample rate, tone frequency and amplitude);
- render the synthetic tone into a preallocated block;
- measure peak level and set a clipping flag;
- push samples into one preallocated 65,536-sample SPSC FIFO per active
  consumer, and increment a bounded drop counter when a FIFO is full.

The callback must not:

- allocate or free heap memory, including indirectly through a container, a
  `std::function`, or a JUCE type that owns storage;
- acquire a lock, or wait on a condition variable, semaphore, or another thread;
- make a blocking system call — file, socket, or `sleep`;
- log, or touch the UI;
- run FFT or pitch detection, or any other unbounded work;
- throw. The function is `noexcept`, so an exception escaping it terminates.

Consumers drain their own FIFO from a message-thread timer and analyse there, so
a slow tool cannot block capture or another tool. Device start, stop, and
sample-rate changes travel by atomics polled on a timer rather than by shared
mutable state. A full FIFO drops the newest block and counts it; it never blocks
and never overwrites unread data.

`AudioInputService.cpp` rejects unsupported targets at compile time with
`is_always_lock_free` assertions for every atomic type the callback reads.

## How it is enforced

Until 2026-08 the rules above were prose, checked only by code review. They are
now checked by a tool.

`audioDeviceIOCallbackWithContext` is declared `noexcept
PRACTICE_TAKES_NONBLOCKING`. Under Clang 20 or newer that macro
(`src/platform/audio/RealtimeSafety.h`) expands to `[[clang::nonblocking]]`;
under any other compiler it expands to nothing, so the GCC build that ships is
unaffected.

The **Realtime** job in `.github/workflows/sanitizers.yml` runs on every pull
request touching `src/**`, the CMake inputs, or that workflow. It builds
`PracticeTakesTests` with clang-20 and `-fsanitize=realtime` in its own build
tree (`PRACTICE_TAKES_SANITIZE=realtime`), then runs the `[callback]` cases.
RealtimeSanitizer aborts the process when a function marked non-blocking
allocates, takes a lock, or makes a blocking call, so such a change fails the
pull request that introduces it.

Both halves of that were verified by deliberately breaking them, not by
observing a pass:

| Probe                                        | Result                                                            |
| -------------------------------------------- | ----------------------------------------------------------------- |
| `std::vector` constructed in the callback     | `unsafe-library-call ... malloc`, exit 43, naming the callback and the test |
| uncontended `std::mutex` locked in the callback | `unsafe-library-call ... pthread_mutex_lock`, exit 43              |

The job also fails if the `[callback]` cases stop existing. RealtimeSanitizer
observes what runs; a check against a callback nobody calls reports success and
means nothing, which is the failure this enforcement exists to remove.

## What this does not cover

Enforcement is real but partial. Read it as "these paths, on this toolchain",
not as "the contract holds".

- **Only the paths the harness drives.** RTSan sees the branches
  `src/tests/platform/audio/AudioInputServiceTests.cpp` executes: output
  clearing, mute, gain, the FIFO push, zero samples, and null input pointers.
  The synthetic-tone branch, the multi-consumer loop with more than one active
  consumer, and every device-lifecycle path are not driven, so an allocation
  added there would not be caught. Widening the harness is #116.
- **Clang only.** The shipping build is GCC, where the annotation is inert. The
  check happens on a different toolchain from the one users run.
- **Runtime, not static.** Nothing verifies that a newly called function is
  itself non-blocking unless a test reaches it. Adding a call to the callback
  does not by itself get that call checked.
- **Not the whole audio thread.** Only this one function carries the
  annotation.

## The MIDI callback

`MidiInputService::handleIncomingMidiMessage` is bound by the same rules. What
it may do:

- read the incoming message's raw bytes through `getRawData()` and
  `getRawDataSize()`;
- translate them into a `midi::MidiEvent`, which is a trivially copyable POD
  with no owning members — `static_assert`ed, so that stops being true at
  compile time rather than quietly;
- increment one atomic counter;
- push one event into each active consumer's preallocated 1,024-event SPSC
  FIFO.

What it must not do is the same list as above, plus one specific to MIDI:

- **never copy the `juce::MidiMessage`.** It owns a heap allocation for any
  payload over four bytes, so a copy is an allocation on a driver callback. The
  callback reads the message and lets it go.

Two differences from the audio FIFO, both deliberate:

- **`midi::MidiEventFifo` drops the oldest event on overflow**, where
  `AudioSampleFifo` drops the newest block. No MIDI event is safe to lose —
  losing a note off leaves a note held forever, losing a note on leaves a note
  off for a note nobody started — so it drops the past rather than the future,
  because that at least re-converges within one key or pedal release.
- **It counts what it dropped, per consumer.** A consumer tracking which notes
  are held compares that count against what it last saw and resets its state
  when it has moved, rather than believing a picture it cannot tell is corrupt.

Enforcement is the same mechanism and has the same hole: the annotation is
checked by RealtimeSanitizer only where a test drives the callback. For MIDI
that is `src/tests/platform/midi/MidiInputServiceTests.cpp`, through
`MidiInputServiceCallbackAccess`, which covers translation, the fan-out to one
and to several consumers, a consumer overflowing, and the messages that
translate to nothing. **Not covered:** opening, closing, or enumerating a
device, and anything a real driver does that a synthetic byte array does not.

## The other runtime legs

Address, Undefined, and Thread sanitizer legs cover the rest of the suite —
what they run, on which trigger, and what happens when a scheduled run goes
red, is in
[Code quality and editor setup](../quality/QUALITY.md#runtime-verification-sanitizers).
