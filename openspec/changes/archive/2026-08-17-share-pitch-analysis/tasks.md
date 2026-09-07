## 1. Relocate PitchDetector to platform

- [x] 1.1 Move `src/features/analysis/tuner/PitchDetector.{h,cpp}` to
      `src/platform/audio/PitchDetector.{h,cpp}`; update the two source lists
      in `CMakeLists.txt` (`PracticeTakes` target and `PracticeTakesTests`
      target).
- [x] 1.2 Move `src/tests/features/analysis/tuner/PitchDetectorTests.cpp` to
      `src/tests/platform/audio/PitchDetectorTests.cpp`; update its path in
      `CMakeLists.txt` and its `#include` of the header it tests.
- [x] 1.3 Update `#include` paths that referenced the old location:
      `src/features/analysis/harmonics/HarmonicAnalyzer.h`,
      `src/tests/platform/audio/SyntheticToneTests.cpp`.
- [x] 1.4 `python3 tools/scripts/quality/check_test_layout.py` passes.

## 2. SharedPitchAnalysis service

- [x] 2.1 Add `src/platform/audio/SharedPitchAnalysis.{h,cpp}`: private
      `AudioInputService::Listener` + `juce::Timer` at 20 Hz, owning a drain
      buffer, a 4096-sample analysis window, and one `PitchDetector`. Port the
      drain/window logic straight from `TunerComponent::drainAudioFifo()`
      before it is deleted in task 3.
- [x] 2.2 Expose `[[nodiscard]] PitchDetector::Result latestResult() const
      noexcept`, backed by atomic fields for frequency and input level.
- [x] 2.3 Add both files to `CMakeLists.txt` (both targets), alongside the
      existing `platform/audio/*` entries.
- [x] 2.4 Add `src/tests/platform/audio/SharedPitchAnalysisTests.cpp`
      covering: a result is available after feeding a synthetic tone through
      an `AudioInputService`; `latestResult()` before any audio arrives
      returns a zero/no-signal result without crashing; the service does not
      register more than one consumer slot.

## 3. Wire the service into ToolServices and MainComponent

- [x] 3.1 Add `SharedPitchAnalysis& pitchAnalysis` to
      `src/application/tools/ToolServices.h`.
- [x] 3.2 `MainComponent` owns `SharedPitchAnalysis pitchAnalysis`, declared
      after `audioInputService` and before `liveTools` — do not disturb the
      existing destruction-order comment/guarantee; extend it to mention the
      new member.
- [x] 3.3 Update the two `ToolServices{...}` construction sites
      (`MainComponentWorkspaceSnapshot.cpp`,
      `MainComponentWorkspacePresentation.cpp`) to pass `pitchAnalysis`.
- [x] 3.4 Update the `tuner` and `harmonic-analyzer` factories in
      `src/application/tools/BuiltInTools.cpp` to pass `services.pitchAnalysis`
      through to their constructors.

## 4. TunerComponent stops owning detection

- [x] 4.1 Constructor takes `SharedPitchAnalysis&` in addition to
      `AudioInputService&`; store a reference.
- [x] 4.2 Delete `pitchDetector`, `drainBuffer`, `analysisBuffer`,
      `fifoCapacity`, `analysisWindowSize`, `currentSampleRate`, and
      `drainAudioFifo()`.
- [x] 4.3 `audioInputAboutToStart` drops the now-deleted buffer fill and
      sample-rate store; keeps `discardPendingSamples(this)`.
- [x] 4.4 `timerCallback()` calls `audioInputService.discardPendingSamples(this)`
      every tick (the tool remains a registered `Listener` for mic-state text,
      but no longer drains samples), then reads
      `pitchAnalysis.latestResult()` and feeds it to `pitchTracker` exactly as
      the locally-computed result was fed before.
- [x] 4.5 Confirm no remaining reference to the deleted members
      (`grep -rn "currentSampleRate\|drainAudioFifo\|analysisBuffer\|drainBuffer" src/features/analysis/tuner/`).

## 5. HarmonicAnalyzer / HarmonicAnalyzerComponent take the shared result

- [x] 5.1 `HarmonicAnalyzer::analyze()` gains a `PitchDetector::Result pitch`
      parameter; delete the owned `pitchDetector` member and its internal
      `detect()` call; every other line is unchanged.
- [x] 5.2 `HarmonicAnalyzerComponent` constructor takes `SharedPitchAnalysis&`
      in addition to `AudioInputService&`; its own FIFO registration and drain
      are unchanged (it still needs raw samples for its own FFT).
- [x] 5.3 `HarmonicAnalyzerComponent::timerCallback()` reads
      `pitchAnalysis.latestResult()` once per tick and passes it into
      `analyzer.analyze(samples, sampleRate, pitch)`.

## 6. Tests and verification

- [x] 6.1 Update/extend `PitchDetectorTests.cpp` at its new path — behavior
      unchanged, path only, but confirm it still builds and passes.
- [x] 6.2 Update any existing `TunerComponent`/`HarmonicAnalyzer` tests that
      construct these types directly for the new constructor/signature.
- [x] 6.3 `cmake --build build --target PracticeTakesTests --parallel` and
      `ctest --test-dir build --output-on-failure` — full suite green.
- [x] 6.4 `python3 tools/scripts/quality/check_test_layout.py`.
- [x] 6.5 `pre-commit run clang-format --all-files` on touched files.
- [x] 6.6 Manual smoke: `./tools/scripts/build/build-and-run.sh`, open both
      the tuner and the harmonic analyzer against the same input (a
      synthetic tone or a sustained note), confirm both show consistent
      pitch, and that disconnecting/reconnecting the microphone still updates
      both tools' status text immediately.

## 7. Close out

- [x] 7.1 `npx openspec validate share-pitch-analysis --strict`.
- [ ] 7.2 Note in the PR description that this closes the last acceptance
      criterion on issue #30 ("Share analysis data where practical instead of
      duplicating expensive work").
