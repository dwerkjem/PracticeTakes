## Context

Three facts, established by inspection rather than assumed.

**Nothing observes the concurrency tests.** `grep -rln "sanitize\|asan\|ubsan\|tsan\|valgrind"` over `.github/`, `tools/cmake`, and `CMakeLists.txt` returns nothing. Meanwhile `src/tests/platform/audio/AudioSampleFifoLoadTests.cpp` runs three `[.load][fifo]` cases with a real producer and consumer on separate threads, and its header comment says outright that they exist to give a race detector something to watch.

**The audio callback has never run in a test.** `AudioInputService.cpp` appears in `CMakeLists.txt` at line 273 — inside the `PracticeTakes` application target, which ends at line 313. `add_executable(PracticeTakesTests ...)` starts at line 354 and does not include it. `coverage_sources.py` lists it among the untested translation units, and no file under `src/tests/` mentions `AudioInputService` or `audioDeviceIOCallbackWithContext`.

**The toolchain is GCC.** `gcc 15.2.0`, no clang installed. GCC supports ASan, UBSan, and TSan. It does not support RealtimeSanitizer, which is clang-20+.

The third fact and the second interact badly with the naive reading of #115. That issue lists "no ASan, UBSan, or TSan build" and "the audio-thread no-allocation rules are unverified by tooling" as though the first bullet addresses the second. It does not: none of those three sanitizers says anything about allocating on a real-time thread.

## Goals / Non-Goals

**Goals**

- A pull request that introduces a memory error, undefined behaviour, or a real-time-safety violation fails before it merges.
- A data race in the sample FIFO is detected by a machine rather than by a user hearing a glitch.
- The audio-thread contract has one executable enforcement point.
- A red scheduled run is discoverable without opening the Actions tab.

**Non-Goals**

- Raising coverage. The harness exists to make RTSan meaningful, not to close #116.
- Retrofitting the two existing scheduled workflows — that is #159.
- Changing the audio-thread rules.

## Decisions

### 1. ASan + UBSan on pull requests; TSan on a schedule and on pushes to `main`

The split is by cost, not by importance. ASan is roughly 2× slower and UBSan around 20%; both combine in one binary and run the ordinary suite. TSan is 5–15× slower and needs its own tree, because ASan and TSan both rewrite memory layout and cannot coexist.

Eleven workflows already run on every pull request. Adding a concurrency soak to that budget buys less than it costs, given the soak defaults to 1500 ms per case (`AudioSampleFifoLoadTests.cpp:50-60`) but stretches under instrumentation.

*Alternative rejected:* everything on pull requests. It is the strictest option and the one that most reliably gets switched off six weeks later when someone is waiting on CI.

*Alternative rejected:* everything nightly. It preserves the exact property this change exists to remove — you merge, then find out.

Running TSan on pushes to `main` as well as nightly is what makes the nightly useful. A weekly-only run that goes red hands you a week of commits and no way to attribute the failure.

### 2. RealtimeSanitizer runs on pull requests, unlike TSan

The cost argument that moves TSan to a schedule does not transfer. RTSan is an interposition layer, and its target is one function invoked a handful of times by a harness, not a multi-second soak.

The value argument runs the other way too. A real-time-safety violation is not a rare scheduling accident that a nightly might happen to catch — it is a line of code someone wrote. It is deterministically detectable on the pull request that introduces it, and detecting it the next morning means it is already on `main`.

### 3. The callback harness is a slice of #116, taken deliberately

RTSan observes code that runs. With no test invoking `audioDeviceIOCallbackWithContext`, an RTSan leg would report success against zero executions — manufacturing exactly the false confidence this change exists to remove, and doing it under a banner that says the contract is now enforced.

So `AudioInputService` joins the test target and a test drives the callback. That is #116's work, pulled forward. Naming it here, in advance, is the difference between a considered dependency and scope discovered halfway through the branch.

The harness is deliberately minimal: construct the service, hand it input and output buffers, call the callback, assert the documented behaviour (output cleared, mute respected, samples reaching the FIFO). Everything else `AudioInputService` does — device lifecycle, gain, level metering, dropout accounting — stays for #116.

### 4. A suppression file, not a disabled sanitizer

ASan enables LeakSanitizer by default on Linux. JUCE, X11, ALSA, and the graphics drivers will report leaks this project cannot fix.

`tools/sanitizers/lsan.supp` is version-controlled, and every entry carries a comment naming the dependency and the reason. A suppression is a decision to ignore something, and a decision to ignore something belongs in review.

*Alternative rejected:* `detect_leaks=0`. It removes third-party noise by removing leak detection in our own code as well, which is the half worth having.

*Alternative rejected:* treat every report as blocking and handle them as they appear. The first pull request to add the leg is then blocked by driver internals, and the realistic resolution is that the leg gets removed.

### 5. Spike clang before committing to RTSan, and close this change even if it fails

The project has never been compiled with clang. JUCE plus an unfamiliar compiler is where warnings-as-errors are discovered. That is a real risk and it is not a sanitizer risk, so it should not be found halfway through the sanitizer work.

The spike is throwaway: build `PracticeTakesTests` with clang-20 in CI and read the output. If it needs real work, RTSan becomes its own issue blocked on a "build under clang" issue, and this change closes with ASan, UBSan, TSan, and the harness delivered.

Three working legs must not sit unmerged behind a toolchain problem.

### 6. The annotation must be inert under GCC

`[[clang::nonblocking]]` behind a macro that expands to nothing when the compiler does not recognise it. The GCC build — which is the build that produces releases — must be unaffected by an annotation that only one toolchain reads.

## Risks / Trade-offs

- **TSan on a schedule means races can merge.** Accepted, and mitigated by the push-to-`main` leg and the auto-opened issue. The alternative costs every pull request a concurrency soak.
- **The harness changes the coverage denominator.** `AudioInputService.cpp` entering the test target moves the instrumented-TU count and the headline percentage. #116's 80% target is measured from 58.1%; this change moves the floor before that work starts. Worth stating so the number is not read as progress on #116.
- **RTSan is only as good as what the harness exercises.** It observes the paths the test drives. A violation on a branch the harness never takes is not detected. This is a real limit and the honest framing is "the contract is enforced on the paths under test", not "the contract is enforced".
- **Suppression files rot.** An entry added for a dependency version can outlive the leak. No mitigation beyond the comment requirement; worth revisiting if the file grows.
- **The clang spike may find a lot.** Then RTSan slips, and this change delivers three quarters of what #115 promised. Better than the alternative, which is #115 open indefinitely.

## Migration Plan

1. Spike clang-20 in CI. Read the result before writing anything else.
2. ASan + UBSan leg. Suppression file as findings appear.
3. TSan leg, scheduled and on push to `main`, with failure → issue.
4. Harness: `AudioInputService` into the test target, a test that drives the callback.
5. RTSan leg and the annotation, only if step 1 was clean.

Steps 2 and 3 are independent of 1 and can land first. Step 5 depends on 1 and 4.

## Open Questions

- **Is clang-20 available on the runners, and does the project build with it?** Step 1 answers this. Everything about RTSan is provisional until it does.
- **Does RTSan need the harness to drive more than the happy path** to be worth the CI slot? Answerable only once it runs; the minimal harness is the starting point, not the intended end state.
