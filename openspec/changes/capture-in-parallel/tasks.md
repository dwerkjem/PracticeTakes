## 1. More than one display at a time

- [ ] 1.1 Confirm `virtual_display` can be entered several times concurrently, each getting its own number
- [ ] 1.2 Tests for concurrent selection that run without Xvfb installed

## 2. The pass takes workers

- [ ] 2.1 A worker count on `CapturePass`, defaulting to 1 so nothing changes unasked
- [ ] 2.2 Each worker enters its own display and starts its own application
- [ ] 2.3 The `digests` and `sizes` maps and the store are shared behind one lock (design decision 2)
- [ ] 2.4 A worker that dies records the surfaces it did not reach as failures, and the others finish

## 3. The command

- [ ] 3.1 `--workers N` on `capture`, defaulting to 1, never derived from the processor count (design decision 4)
- [ ] 3.2 Reject a count below 1 rather than silently treating it as 1

## 4. Tests

- [ ] 4.1 A plan captured with several workers covers exactly the same surfaces as with one, each once
- [ ] 4.2 `duplicate_problem` still fires when the two colliding captures came from different workers — the check this change most risks weakening
- [ ] 4.3 `geometry_problem` still fires across workers

## 5. Verification

- [ ] 5.1 `python tools/scripts/run_tests.py` green
- [ ] 5.2 A real full sweep at 1, 2, and 4 workers: same captures, and the wall-clock difference recorded
- [ ] 5.3 Confirm a deliberately duplicated surface is still caught with several workers running
- [ ] 5.4 Note the worker count above which the settle check starts failing, if one is reached
