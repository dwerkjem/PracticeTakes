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

- [ ] 3.1 Separate build tree; build `PracticeTakesTests` with TSan
- [ ] 3.2 Run `[.load]` only; check the soak duration under instrumentation and tune the env override if the job is slow
- [ ] 3.3 Triggers: `schedule` plus `push` to `main`
- [ ] 3.4 On failure, open an issue with `area:testing` and `priority:p1` naming the workflow, run URL, and failing step
- [ ] 3.5 Comment on the existing issue rather than opening a duplicate when one is already open
- [ ] 3.6 Confirm it reports a race by weakening FIFO synchronisation deliberately, then revert

## 4. Run the audio callback under observation

- [ ] 4.1 Add `src/platform/audio/AudioInputService.cpp` and `.h` to `add_executable(PracticeTakesTests ...)`
- [ ] 4.2 Add `src/tests/platform/audio/AudioInputServiceTests.cpp`: construct the service, supply input and output buffers, invoke `audioDeviceIOCallbackWithContext`
- [ ] 4.3 Assert the documented behaviour — output cleared, mute honoured, samples reaching the FIFO
- [ ] 4.4 Keep it minimal: device lifecycle, gain, metering, and dropout accounting stay for #116
- [ ] 4.5 Confirm the test layout checker still passes and note the new instrumented-TU count for #116's baseline

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
