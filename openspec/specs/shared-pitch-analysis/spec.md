# shared-pitch-analysis Specification

## Purpose

Defines the contract for how pitch detection is computed once and shared
between the tuner and the harmonic analyzer, instead of each tool
independently owning a detector and duplicating the FFT-based autocorrelation
over its own, independently-drained audio window. Covers the refresh cadence,
how a consuming tool reads the latest result, and the guarantees that carry
over from before this existed: microphone-state responsiveness and the
audio-thread contract.

## Requirements
### Requirement: One pitch detection per cadence
The application SHALL run at most one pitch-detection analysis (one
`PitchDetector::detect()` call) per refresh cadence, regardless of how many of
the tuner and harmonic analyzer tools are currently open.

#### Scenario: Both pitch-consuming tools open at once
- **WHEN** the tuner and the harmonic analyzer are both docked or floating at
  the same time and a sustained tone is playing
- **THEN** exactly one shared pitch-detection computation runs per refresh
  tick, and both tools' displayed fundamental frequency for that tick derive
  from that same computation

#### Scenario: Only one pitch-consuming tool open
- **WHEN** only the tuner (or only the harmonic analyzer) is open
- **THEN** pitch detection still runs once per refresh tick, and behaves
  exactly as it does today for that tool

### Requirement: Consumers read a live snapshot
A tool that consumes shared pitch analysis SHALL read the most recently
computed result on its own refresh timer rather than being blocked on, or
required to synchronize with, the shared analysis's timer.

#### Scenario: Consumer ticks between shared refreshes
- **WHEN** a consuming tool's own timer fires between two shared
  pitch-analysis refreshes
- **THEN** it reads the latest available result without waiting, and does not
  crash or block

### Requirement: Mic-state responsiveness is unaffected
Tools that no longer drain their own audio FIFO for pitch purposes SHALL still
react immediately to microphone state changes (disconnected, muted, clipping,
active) and SHALL NOT cause the application's dropped-sample diagnostics to
grow while idle.

#### Scenario: Microphone disconnects while the tuner is open
- **WHEN** the microphone is disconnected while the tuner tool is open
- **THEN** the tuner shows a disconnected message immediately, the same as
  before this change

#### Scenario: A tool stops draining its own audio samples
- **WHEN** a tool that used to drain its own audio FIFO for pitch detection no
  longer does so, but remains registered for microphone-state notifications
- **THEN** the application's aggregate dropped-analysis-block and
  dropped-analysis-sample counts do not grow because of that tool sitting idle

### Requirement: Audio-thread contract is preserved
Shared pitch analysis SHALL perform all detection work on the message thread,
never inside the real-time audio device callback.

#### Scenario: Shared analysis runs under the RealtimeSanitizer job
- **WHEN** the real-time audio callback executes with RealtimeSanitizer
  observing it
- **THEN** no allocation, lock, or blocking call attributable to shared pitch
  analysis is reported
