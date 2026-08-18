## Why

The tuner and the harmonic analyzer each own an independent `PitchDetector`
and each drive it from their own message-thread timer over their own,
independently-drained FIFO. Opening both at once runs the same 4096-sample
autocorrelation-via-FFT twice for no benefit: the two windows are not even
guaranteed to be phase-aligned, since each tool drains its per-listener FIFO
on its own cadence, so the two tools can show slightly different pitch
readings for the same note at the same instant. Issue #30's last open
acceptance criterion is "Share analysis data where practical instead of
duplicating expensive work"; pitch detection is the one piece of expensive,
duplicated work both tools do today.

## What Changes

- Add a `SharedPitchAnalysis` service, owned by `MainComponent` alongside
  `AudioInputService`, that registers one `AudioInputService::Listener`, drains
  one FIFO on one message-thread timer, and runs `PitchDetector::detect()`
  once per cadence.
- Add `SharedPitchAnalysis& pitchAnalysis` to `ToolServices`, following the
  pattern the struct's own comment already describes for exactly this
  situation ("adding a service later is one field here").
- `TunerComponent` stops owning a `PitchDetector` and stops registering its
  own FIFO listener for pitch samples; it reads the shared service's latest
  `PitchDetector::Result` each timer tick and keeps its own `PitchTracker`
  smoothing on top, unchanged.
- `HarmonicAnalyzer` stops owning a `PitchDetector` and stops calling
  `detect()` itself; `analyze()` takes the shared result as an input instead
  of computing it. Its own harmonic-bar FFT (window/order 12) is untouched —
  that work is not duplicated anywhere and stays put.
- No change to the audio-thread contract: detection still runs on a
  message-thread timer after draining a FIFO, never in
  `audioDeviceIOCallbackWithContext`.

## Capabilities

### New Capabilities

- `shared-pitch-analysis`: a single-producer, at-most-two-consumer pitch
  detection service — its ownership, refresh cadence, and the contract a tool
  gets when it asks for the latest result.

### Modified Capabilities

None. No existing `openspec/specs/*` capability documents tuner or harmonic
analyzer behavior at the requirement level; this only changes an internal
implementation detail behind an unchanged UI contract (both tools' displayed
behavior is required to not regress, not to change).

## Impact

- `src/application/tools/ToolServices.h` — new field.
- `src/application/shell/MainComponent.h`/`.cpp` — new owned service,
  declared after `audioInputService` and before `liveTools` so the
  destruction-order guarantee ("a tool cannot outlive its shared services")
  keeps covering it.
- `src/application/shell/ui/workspace/shell/MainComponentWorkspaceSnapshot.cpp`
  and `MainComponentWorkspacePresentation.cpp` — the two `ToolServices{...}`
  construction sites gain the new argument.
- `src/application/tools/BuiltInTools.cpp` — the `tuner` and
  `harmonic-analyzer` factories pass the new service through.
- New `src/platform/audio/SharedPitchAnalysis.{h,cpp}`.
- `src/features/analysis/tuner/TunerComponent.{h,cpp}` — drop owned
  `PitchDetector` and its dedicated pitch-sample FIFO draining; consume the
  shared result.
- `src/features/analysis/harmonics/HarmonicAnalyzer.{h,cpp}` and
  `HarmonicAnalyzerComponent.{h,cpp}` — same.
- New `src/tests/platform/audio/SharedPitchAnalysisTests.cpp`.
- `PitchDetector` itself (`src/features/analysis/tuner/PitchDetector.{h,cpp}`)
  is unchanged; only who owns and calls it moves.
