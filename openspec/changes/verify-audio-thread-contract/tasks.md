## 1. Spike clang — the gate on everything RTSan

- [x] 1.1 Add a throwaway CI job that installs clang-20 and configures `PracticeTakesTests` with it; capture the full output
- [x] 1.2 Confirm `-fsanitize=realtime` is accepted by that clang, **and that it detects a real allocation** — a flag that merely parses proves nothing
- [x] 1.3 Record the verdict in the change: **clean** — clang 20.1.2 from Ubuntu apt, configures, builds, 464/464 pass, RTSan verified to detect. See design decision 7
- [x] 1.4 ~~If it is not clean~~ — not triggered; section 5 stays. **If it is not clean**, open a "build under clang" issue and an RTSan issue blocked on it, drop tasks 5.x from this change, and continue — sections 2 to 4 do not depend on this

## 2. ASan and UBSan on pull requests

- [x] 2.1 `PRACTICE_TAKES_SANITIZE` cache variable — a single-valued string, so ASan and TSan are mutually exclusive *structurally* rather than by a check. Guards verified rejecting an unknown value, sanitizer+coverage in one tree, and `realtime` under GCC
- [x] 2.2 Workflow job: configure with ASan+UBSan, build `PracticeTakesTests`, run the full suite
- [x] 2.3 Add `tools/sanitizers/lsan.supp` with a comment per entry; wire `LSAN_OPTIONS`
- [x] 2.4 Confirmed: `heap-use-after-free`, exit 1. The first probe was constant-propagated away at `-O1` and reported success — reading through a `volatile` pointer defeats it
- [x] 2.5 Confirm the job fails on deliberate signed overflow, then remove it
- [x] 2.6 Confirmed: a deliberate 4096-byte leak from our own code fails despite the fontconfig suppression, and every frame in the real report is `libfontconfig`

## 3. TSan on a schedule and on pushes to main

- [x] 3.1 Separate build tree; build `PracticeTakesTests` with TSan
- [x] 3.2 Run `[.load]` only. Measured **2 s** under TSan at the 1500 ms soak default — no tuning needed
- [x] 3.3 Triggers: `schedule` plus `push` to `main`
- [x] 3.4 On failure, open an issue with `area:testing` and `priority:p1` naming the workflow, run URL, and failing step
- [x] 3.5 Comment on the existing issue rather than opening a duplicate when one is already open
- [x] 3.6 Confirmed: weakening `writePosition.store` from release to relaxed produced `WARNING: ThreadSanitizer: data race`, exit 66, naming both accesses. Reverted byte-identical

## 3b. Bound the backpressure assertions (design decision 8)

- [x] 3b.1 Replace `droppedBlocks() == 0` with a bound at all three retry-loop sites; the counter measures rejected-and-retried pushes, not lost data
- [x] 3b.2 Report the observed rejection count so drift is visible without being fatal
- [x] 3b.3 Keep `stalled.droppedBlocks() > 0` exact — it is the point of that test, not a scheduling outcome
- [x] 3b.4 Confirm all five cases pass on an ordinary build and under TSan

## 4. Run the audio callback under observation

- [x] 4.1 Added to the test target. Required converting the header off the generated `JuceHeader.h` umbrella to modular `juce_audio_devices`/`juce_core`/`juce_events` includes — that umbrella is produced for the application target only, which is the mechanical reason this file had never been testable
- [x] 4.2 Add `src/tests/platform/audio/AudioInputServiceTests.cpp`: construct the service, supply input and output buffers, invoke `audioDeviceIOCallbackWithContext`
- [x] 4.3 Assert the documented behaviour — output cleared, mute honoured, samples reaching the FIFO
- [x] 4.4 Keep it minimal: device lifecycle, gain, metering, and dropout accounting stay for #116
- [x] 4.5 Layout checker passes. Instrumented TUs **26 of 47 (55.3%) → 27 of 47 (57.4%)**. #116's quoted "25 of 43, 58.1%" is stale — the denominator has grown, so its 80% target should be measured against 47

## 5. RealtimeSanitizer — only if 1.3 was clean

- [ ] 5.1 Portability macro for `[[clang::nonblocking]]`, inert under GCC. Attribute goes **after** the parameter list, and needs `noexcept` alongside it — decide between annotating the override directly or a private `noexcept` helper (design decision 6)
- [ ] 5.2 Annotate `audioDeviceIOCallbackWithContext`
- [ ] 5.3 Confirm the GCC build and the GCC test run are unaffected
- [ ] 5.4 RTSan build tree and a pull-request job running the harness
- [ ] 5.5 Confirm it fails on a deliberate allocation in the callback, then remove it
- [ ] 5.6 Confirm it fails on a deliberate lock in the callback, then remove it

## 6. Documentation

- [ ] 6.1 Record in `docs/development/performance/audio-thread-safety.md` that the contract is enforced, by what, on which trigger, and — honestly — that enforcement covers only the paths the harness drives
- [ ] 6.2 Note the sanitizer legs in `docs/development/quality/QUALITY.md`
- [ ] 6.3 Correct #115's stale reference to `docs/development/performance-audio-thread-safety.md`

## 7. Verification

- [ ] 7.1 `ctest --test-dir build --output-on-failure` still green in the ordinary GCC tree
- [ ] 7.2 Every new leg observed passing and observed failing, not just passing
- [ ] 7.3 Note the coverage figure before and after task 4.1
