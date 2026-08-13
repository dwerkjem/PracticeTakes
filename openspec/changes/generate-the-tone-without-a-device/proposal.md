## Why

The synthetic tone is generated inside the audio callback, so it only exists
while a real input device is open. That is a strange dependency for a signal
nobody hears: the tone is never played, and the device it needs is only there to
call the function that makes it.

It costs something measurable. One application at a time holds the input device
— with eight capture workers running, seven report none — so the surfaces that
carry a tone can only be photographed by whichever worker won the device. They
are serialised onto that one worker while everything else runs in parallel, and
they are the slow surfaces: each waits a warmup for the tool to have a history
to draw.

It has already produced a wrong capture. A worker without the device
photographed `tuner-in-tune` as a blank graph saying "waiting for the
microphone", and the run counted it as captured. That particular hole is closed
by routing those surfaces away from deaf workers, but the routing exists only
because the tone needs a device it has no use for.

## What Changes

- **The tone is generated whether or not a device is open.** When one is asked
  for and there is no device delivering samples, the same generator fills the
  same FIFOs from a source of its own.
- **A tool cannot tell the difference.** Same samples, same rate, same
  lifecycle notifications — a tool being fed a tone is analysing input, and the
  interface says so.
- **One producer at a time, always.** The device callback and the tone source
  never fill a FIFO at once; the FIFOs are single-producer by construction and
  stay that way.
- **Nothing changes when there is no tone.** A tone is asked for by the test
  control channel and by nothing else, so an ordinary run never starts a source.

## Non-goals

- **Not a change to the audio callback's contract.** It stays `noexcept`,
  non-blocking, and does no more work than it does now.
- **Not audible.** The tone still goes only into the analysis FIFOs; the
  device's output is cleared and never written.
- **Not a replacement for capturing with a real device.** A capture with real
  input is the more honest one; this is for surfaces that need a signal to show
  a tool doing something.
- **Not a way to run the application without audio.** The device is still
  opened, still recovered, and still reported.

## Capabilities

### Added Capabilities

- `synthetic-input`: a signal the analysis path can be fed without a device,
  and what a tool must not be able to tell about where its samples came from.
