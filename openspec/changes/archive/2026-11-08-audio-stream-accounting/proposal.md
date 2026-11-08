## Why

`multi-instance-tools` capped microphone tools at `AudioInputService`'s eight
sample slots, counting every tool that registered a listener. That limit was
neither realistic nor honest about what used it (#317):

- **The tuner held a slot it never read.** It takes its pitch from
  `SharedPitchAnalysis`, and only registered a listener to hear about mute and
  disconnection. Each tuner cost one of eight 256 KB FIFOs, and the audio
  callback filled each one on every block for nobody.
- **Seven copies at most, in practice.** `SharedPitchAnalysis` holds one slot,
  so the cap fell wherever the tools happened to land, and "no audio capacity
  left" didn't say who held the capacity.
- **No per-tool bound.** A hand-edited workspace could name `tuner#40`.

The maintainer asked, on 2026-11-08, for a more realistic limit that counts what
is actually using it.

## What Changes

- **State-only listeners.** `AudioInputService::addStateListener`. It is told
  about start, stop and state changes, holds no FIFO, and the audio callback
  never visits it. The tuner uses it.
- **`ToolDefinition::readsAudioSamples`** replaces `usesAudioInput`. It is set
  only for the tools that run their own FFT: the spectrogram and the harmonic
  analyzer.
- **Sixteen sample streams** instead of eight. That is one for shared pitch
  detection, room for six spectrograms and six harmonic analyzers, and three to
  spare for the sample tools the roadmap adds. The array is preallocated (4 MB),
  and the callback's per-slot cost for an idle stream is one atomic load.
- **At most six copies of any tool**
  (`ToolInstanceAllocator::maximumInstancesPerTool`). The normalizer enforces it
  on restore too.
- **The refusal says why.** `canOpenAnother` returns a reason, not a bool:
  - at the limit, the menu says "Open another (6 is the most)";
  - out of streams, the menu says "Open another (no audio stream free)".
    Choosing it shows an alert naming who holds the streams ("All 16 audio
    streams are in use, by 6 Harmonic Analyzers, 6 Spectrograms and…").

  `AudioStreamUse` works this out from what is open, attributing the remainder
  to shared pitch detection.

## Capabilities

### Modified Capabilities

- `tool-registry`: the requirement capping microphone tool instances now
  describes the per-tool limit, sample streams versus state-only listeners,
  sixteen streams, and the explained refusal.

## Impact

- `src/platform/audio/AudioInputService.{h,cpp}`: state-only listeners, sixteen
  slots, `consumerCapacity()`. `listenerSnapshot()` now returns a vector built
  on the message thread. The callback is unchanged.
- `src/features/analysis/tuner/TunerComponent.cpp`: registers as a state-only
  listener.
- `src/application/tools`: `readsAudioSamples`, `maximumInstancesPerTool`,
  `canOpenAnother`, and `AudioStreamUse.h`.
- Shell: menu text, the explained alert, and restore.
- Tests: state-only listeners, capacity, the verdicts, the sentence, and the
  restore cap.
