## 1. Stopping a run

- [ ] 1.1 `CapturePass.run` takes something it can ask whether to stop, checked at each surface boundary (design decision 1)
- [ ] 1.2 Surfaces not reached are left unattempted, not recorded as failures (design decision 2)
- [ ] 1.3 The application and the virtual display are shut down on the way out, as they are on a normal finish
- [ ] 1.4 Tests over the loop: a run stopped part way captures the surfaces before the stop and none after, and reports no failures

## 2. Reaching it from the hub

- [ ] 2.1 A stop control on the run view, shown only while a run is in progress
- [ ] 2.2 An endpoint behind it, refusing politely when no run is in progress
- [ ] 2.3 Escape does the same thing (design decision 3)
- [ ] 2.4 The view says the run was stopped rather than that it finished

## 3. Continuing afterwards

- [ ] 3.1 Confirm running again skips what the stopped run captured — the existing resume, exercised rather than assumed

## 4. Commenting in bulk

- [ ] 4.1 One comment written against every capture in the current selection, using the narrowing the review already has
- [ ] 4.2 One row per capture, so a capture read alone still shows it (design decision 4)
- [ ] 4.3 Tests: a comment over a selection reaches exactly those captures and no others

## 5. Verification

- [ ] 5.1 `python tools/scripts/run_tests.py` green
- [ ] 5.2 Stop a real sweep part way; confirm the captures before it are reviewable and nothing is marked failed
- [ ] 5.3 Run it again and confirm it resumes rather than starting over
- [ ] 5.4 Comment on several captures at once and confirm each carries it
