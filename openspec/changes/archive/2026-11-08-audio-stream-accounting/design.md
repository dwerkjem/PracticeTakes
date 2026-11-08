## Decisions

### A second kind of listener, not a flag on the first

A state-only listener lives in its own `std::vector`, guarded by the same lock
as the slots and touched only on the message thread. The alternative was a flag
on a slot meaning "don't fill this". That would still spend the slot, which was
the whole problem, and would put a new branch in the audio callback. The
callback is unchanged: it walks the slot array as before and never sees the
state listeners.

`listenerSnapshot()` returns a `std::vector` (the sample listeners, then the
state-only ones) instead of a fixed `std::array`. It only ever runs on the
message thread, in `publishState` and `deliverFormatChange`, so an allocation
there is fine. `audio-thread-safety.md`'s rules are about the callback.

### Why sixteen and six

Six copies of a tool is about what a 1280-pixel-wide window shows legibly when
tiled, and more than that becomes a tab strip nobody can scan. With six as the
per-tool limit, the two sample-reading tools need at most 2 × 6 + 1 (shared
pitch) = 13 streams. Sixteen leaves three for the sample tools on the roadmap
(the loudness meter and others). The streams are sized so that the per-tool
limit, which the menu explains, is the one people meet, not the hidden audio
one. Each stream is a 256 KB FIFO, so the array is 4 MB, allocated once with
the service, which lives inside the heap-allocated `MainComponent`. An idle
stream costs the callback one atomic load per block.

### What is using the streams is counted, not asked

The service holds listener pointers, not tool names. The shell knows which open
instances read samples, so `AudioStreamUse` counts them per tool, and attributes
whatever is left (capacity − free − tools) to shared pitch detection, which
always holds one. If that remainder is ever more than one, the sentence says
"N other listeners" rather than guessing.

### "No stream free" stays clickable; "six is the most" doesn't

At the per-tool limit, the only fix is to close a copy of that tool, and the
greyed text says so. With no stream free, the fix depends on which tools are
using streams. Choosing the item shows that breakdown, which a greyed-out item
couldn't.
