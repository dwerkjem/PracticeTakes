# Audio-thread safety

`AudioInputService::audioDeviceIOCallbackWithContext` runs on the real-time
audio thread. Everything below is a hard constraint: a violation does not
produce a test failure or a wrong number, it produces an audible dropout in
someone's practice session.

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

## The other runtime legs

Address, Undefined, and Thread sanitizer legs cover the rest of the suite —
what they run, on which trigger, and what happens when a scheduled run goes
red, is in
[Code quality and editor setup](../quality/QUALITY.md#runtime-verification-sanitizers).
