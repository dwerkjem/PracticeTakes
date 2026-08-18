## Context

`TunerComponent` and `HarmonicAnalyzerComponent` are each `ToolInstancePolicy::single`
(`BuiltInToolCatalog.h`), so at most one of each ever exists — this is a small,
fixed fan-out, not a general N-tool broadcast problem.

Today each owns a `PitchDetector` and drives it independently:

- Each registers as an `AudioInputService::Listener`, which gives it one of the
  fixed `maximumConsumers = 8` FIFO slots (`AudioInputService::ConsumerSlot`,
  `consumerFifoCapacity = 65536`). The audio callback pushes every block into
  every *active* slot's FIFO unconditionally (`AudioInputService::deliverSamples`),
  so each registered listener is real (if bounded) audio-thread work.
- Each drains its FIFO on its own 20 Hz `juce::Timer` into a 4096-sample window
  (`PitchDetector::windowSize`) and calls `PitchDetector::detect()` — an FFT-based
  autocorrelation, the expensive step.
- `HarmonicAnalyzer::analyze()` calls `detect()` internally, then runs a
  *second*, separate FFT (order 12) over the same window to read harmonic-bin
  magnitudes. That second FFT is real additional work (bar heights come from
  nowhere else) and is unaffected by this change.

Because the two tools drain independently-buffered FIFOs on independent 20 Hz
timers, their 4096-sample windows are not phase-aligned even though both are
fed the same microphone stream — so today's setup is both duplicated CPU work
*and* two pitch estimates that can silently disagree when both tools are open.

`PitchDetector` itself already lives in the wrong place for what it is: it is
pure DSP with no tuner-specific behavior, but `src/features/analysis/harmonics/
HarmonicAnalyzer.h` already reaches into `src/features/analysis/tuner/
PitchDetector.h` to use it — exactly what the agent guide's "a `src/features/*`
tool must never reach into another tool's internals" rule exists to prevent.
Adding a `src/platform` service that depends on a `src/features/tuner` header
would compound that violation instead of fixing it.

`ToolServices` (`src/application/tools/ToolServices.h`) already documents that
it is meant to grow this way: "adding a service later ... is one field here
instead of a signature change at every factory." `AudioInputService` is the
existing precedent for a service `MainComponent` owns and hands to tools by
reference, with `MainComponent`'s declaration order (services before
`liveTools`) being what guarantees a tool cannot outlive a service it borrows.

Listener notifications are message-thread-only in practice: `AudioInputService`
only calls `Listener::audioInputAboutToStart/Stopped` from
`deliverFormatChange()`, and `Listener::audioInputStateChanged` from
`publishState()`, and both are only ever called from `AudioInputService`'s own
`juce::Timer::timerCallback()` (confirmed by reading `AudioInputService.cpp`,
not assumed) — the JUCE `AudioIODeviceCallback` hooks that *can* run on a
device/background thread (`audioDeviceAboutToStart`, `audioDeviceStopped`) only
touch atomics and a `formatVersion` counter that the message-thread timer polls.
So a service built the same way `TunerComponent`/`HarmonicAnalyzerComponent`
already are today has no real cross-thread race on its published result; this
design keeps the existing atomic-field idiom anyway (e.g. `TunerComponent`'s
`currentSampleRate`) purely for local consistency with the rest of the file,
not because it is load-bearing.

## Goals / Non-Goals

**Goals**

- Pitch detection — the FFT-based autocorrelation — runs at most once per
  cadence regardless of how many of the two tools are open.
- Both tools see the same fundamental-frequency estimate at the same instant.
- No behavior change visible to a user: same refresh rate, same displayed
  precision, same mic-state responsiveness, same audio-thread contract.
- Fix the pre-existing `harmonics` → `tuner` reach-into instead of adding a
  second one.

**Non-Goals**

- No general pub/sub for N future consumers. Two call sites, one polled getter.
- No change to `PitchTracker` (tuner's own smoothing) or to
  `HarmonicAnalyzer`'s own harmonic-bar FFT — both keep doing exactly what they
  do today, just fed a `PitchDetector::Result` from outside instead of
  computing it themselves.
- No attempt to make the two tools' analysis windows byte-identical to
  `HarmonicAnalyzerComponent`'s own FFT window. See Risks.

## Decisions

**1. `PitchDetector` moves to `src/platform/audio/`.**
It has no tuner-specific behavior (`windowSize`, `Result{frequency,
inputLevel}`, a stateless `detect()`) and is about to be owned by a
`src/platform` service; leaving it under `src/features/analysis/tuner/` and
having platform code depend upward on a feature would be backwards. Its test
moves with it, `src/tests/platform/audio/PitchDetectorTests.cpp`, to keep the
mirror `check_test_layout.py` enforces. Alternative considered: leave it in
`tuner/` and let `SharedPitchAnalysis` depend on it anyway — rejected, it
keeps the same layering violation the harmonics side already has, just adds a
second offender.

**2. A new `SharedPitchAnalysis` service owns the one FIFO registration for
pitch, at `src/platform/audio/SharedPitchAnalysis.{h,cpp}`.**
Shaped like the pitch-handling half of today's `TunerComponent`: private
`AudioInputService::Listener` + `juce::Timer` at 20 Hz (matching both tools'
existing cadence, so no perceptible behavior change), owning the drain buffer,
the 4096-sample analysis window, and one `PitchDetector`. Exposes
`[[nodiscard]] PitchDetector::Result latestResult() const noexcept`. Alternative
considered: a broadcaster/`Listener` interface so consumers get pushed a result
— rejected as unneeded complexity for two consumers that already poll on their
own timers for their own UI refresh; a plain getter each tool reads once per
tick is simpler and just as correct here (Non-Goals).

**3. `ToolServices` gains `SharedPitchAnalysis& pitchAnalysis`.**
`MainComponent` owns `SharedPitchAnalysis pitchAnalysis{audioInputService};`,
declared after `audioInputService` (its constructor registers a listener on
it) and before `liveTools` (tools borrow it), preserving the existing
destruction-order guarantee. The two `ToolServices{...}` construction sites
(`MainComponentWorkspaceSnapshot.cpp`, `MainComponentWorkspacePresentation.cpp`)
and the `tuner`/`harmonic-analyzer` factories in `BuiltInTools.cpp` thread it
through.

**4. `TunerComponent` stops owning a `PitchDetector` and stops draining
samples, but stays an `AudioInputService::Listener`.**
It still needs edge-triggered mic-state notifications for its status text
("Microphone disconnected.", etc.) and `resetPitchTracking()` on a state
transition — `AudioInputService::publishState()` already delivers exactly
that, once per real transition, to every registered `Listener`. Fully
unregistering and switching to polling `audioInputService.inputState()` would
lose that edge-triggering for no benefit. Its `timerCallback()` calls
`audioInputService.discardPendingSamples(this)` — not `readSamples` — every
tick instead of draining into a buffer it no longer has: staying registered
without ever draining would let its FIFO fill in ~1.5 s
(65536 samples ÷ 44100 Hz) and then overflow every subsequent audio callback,
which increments `AudioSampleFifo::droppedBlockCount/droppedSampleCount` —
counters `AudioInputService::droppedAnalysisBlocks/Samples` sums *across all
active consumers* and that the service's own timer compares to decide when to
`sendChangeMessage()` (`AudioInputService.cpp:654`). An idle, undrained FIFO
would silently corrupt that global diagnostic. `discardPendingSamples` costs
one atomic store and keeps the accounting honest. `currentSampleRate` and the
`drainBuffer`/`analysisBuffer`/`fifoCapacity`/`analysisWindowSize` members and
`drainAudioFifo()` are deleted outright — nothing else in `TunerComponent`
reads `currentSampleRate` once it no longer calls `detect()` itself (checked:
the only two uses were setting it in `audioInputAboutToStart` and reading it
for `detect()`).

**5. `HarmonicAnalyzer::analyze()` takes the shared result instead of
computing it.**
New signature:
`Result analyze(std::span<const float, windowSize> samples, double sampleRate,
PitchDetector::Result pitch)`. The owned `PitchDetector pitchDetector;` member
is removed; every other line of `analyze()` — the harmonic-bar FFT, the
spectral centroid, the confidence/inharmonicity math — is unchanged, only
where `pitch.frequency`/`pitch.inputLevel` comes from moves to the caller.
`HarmonicAnalyzerComponent` is otherwise unchanged: it keeps its own FIFO
registration and drain, because it still needs the raw sample window for its
own FFT; it only adds one call to `pitchAnalysis.latestResult()` per tick,
passed straight into `analyzer.analyze(...)`.

## Risks / Trade-offs

- **[Risk] `HarmonicAnalyzerComponent`'s own FFT window and
  `SharedPitchAnalysis`'s window are still two independently-drained 4096-sample
  buffers, so the fundamental used to place harmonic bins is not from the exact
  same samples the bars are computed from.** → Accepted: this is strictly
  better than today (today the *harmonic tool's own* fundamental estimate came
  from yet a third, separately-detected value; after this change there is one
  detector's estimate instead of two disagreeing ones). A sustained tone's
  fundamental does not move meaningfully across two ~93 ms windows ~50 ms
  apart, which is the case pitch detection is for. Matches issue #30's
  "share ... where practical," not "make every window identical."
- **[Risk] `SharedPitchAnalysis` is unconditionally owned by `MainComponent`,
  so it runs its 20 Hz FFT even when neither the tuner nor the harmonic
  analyzer is open — a small always-on cost that does not exist today (today,
  zero pitch-consuming listeners are registered when both tools are closed).**
  → Accepted: `AudioInputService` itself already runs unconditionally at the
  same cadence regardless of open tools, so this follows the existing
  ownership precedent rather than inventing lifecycle-gating machinery for a
  single FFT at 20 Hz, which is far under any budget this application tracks.
- **[Risk] Moving `PitchDetector.{h,cpp}` changes two `#include` paths outside
  this change's direct motivation** (`HarmonicAnalyzer.h`,
  `SyntheticToneTests.cpp`). → Mechanical, caught by the build; listed in
  Impact.

## Migration Plan

Single PR, no data migration, no user-visible behavior change intended (parity
is the point). Rollback is a plain revert — nothing persists new state.
