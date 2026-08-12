## 1. More than one display at a time

- [x] 1.1 Confirm `virtual_display` can be entered several times concurrently, each getting its own number
- [x] 1.2 Tests for concurrent selection that run without Xvfb installed

## 2. The pass takes workers

- [x] 2.1 A worker count on `CapturePass`, defaulting to 1 so nothing changes unasked
- [x] 2.2 Each worker enters its own display and starts its own application
- [x] 2.3 The `digests` and `sizes` maps and the store are shared behind one lock (design decision 2)
- [x] 2.4 A worker that dies records the surfaces it did not reach as failures, and the others finish

## 3. The command

- [x] 3.1 `--workers N` on `capture`, defaulting to 1, never derived from the processor count (design decision 4)
- [x] 3.2 Reject a count below 1 rather than silently treating it as 1

## 4. Tests

- [x] 4.1 A plan captured with several workers covers exactly the same surfaces as with one, each once
- [x] 4.2 `duplicate_problem` still fires when the two colliding captures came from different workers — the check this change most risks weakening
- [x] 4.3 `geometry_problem` still fires in a parallel run — and cannot span
  workers at all, since a surface's resolutions are kept in one share
- [x] 4.4 A worker that dies records its share as failures and the rest finish

## 5. Verification

- [x] 5.1 `python tools/scripts/run_tests.py` green
- [x] 5.2 A real full sweep at 1, 2, and 4 workers: same captures, and the wall-clock difference recorded
- [x] 5.3 Confirm a deliberately duplicated surface is still caught with several workers running —
  and confirm the test fails when the maps are split, which the first version of it did not
- [x] 5.4 Note the worker count above which the settle check starts failing, if one is reached

## 6. What running it found

- [x] 6.1 `ApplicationDriver.send` gets a deadline — a hung application was an unexplained wait on a pipe
- [x] 6.2 `stop` sends `quit` before clearing the process it is quitting; it never had
- [x] 6.3 Record the measured ceiling and its cause (design: *What running it actually found*)
- [x] 6.4 Gate the one moment that touches the device — an application opening it — and let capture overlap
- [x] 6.5 Hand out work as workers come free, rather than splitting the plan in advance
- [x] 6.6 Retire a worker that cannot start instead of retrying under the gate; kill rather than ask
- [x] 6.7 Track the process ids this run started; end those and nothing else
- [x] 6.8 Name the instances a run competes with, and say plainly that nothing will close them
- [ ] 6.9 **Still open:** capturing while the application is open needs a synthetic-input mode
      or device recovery off the message thread — an application change, its own proposal.
      Measured: a *sequential* capture with another instance open captures nothing at all.
- [ ] 6.10 **Deferred:** whether the hub offers a worker count
