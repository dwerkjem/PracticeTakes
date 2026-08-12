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

## 7. The races, and the bar

- [x] 7.1 Every group is taken exactly once under real contention — proven by removing the lock (1223 takes instead of 292)
- [x] 7.2 Two workers never hold the audio gate at once
- [x] 7.3 The shared maps survive being written by every worker
- [x] 7.4 The progress count is not lost between workers — proven by removing the lock
- [x] 7.5 A stop reaches every worker, before and part way through
- [x] 7.6 The fleet loses no registration under concurrent add/remove, and teardown against live workers still ends everything
- [x] 7.7 Replies never cross between drivers, and one hanging driver does not hold up another
- [x] 7.8 Weight a plan by what a surface costs, not by counting surfaces
- [x] 7.9 Estimate what is left from this run's own measured rate, and say nothing until there is one

## 8. The sanitizers, and where a sweep's time goes

- [x] 8.1 Race conditions, memory errors and leaks, and the audio callback offered in the hub, not only in CI
- [x] 8.2 A build tree per sanitizer; RealtimeSanitizer gets Clang, the rest keep the system toolchain
- [x] 8.3 Sanitizer options travel with the suite that needs them, and are asserted to match the workflows
- [x] 8.4 Their own kind, so "run the tests" does not silently mean twenty minutes
- [x] 8.5 Open a state once per surface and palette rather than once per resolution — 72 rebuilds instead of 292
- [x] 8.6 Warm a tool's history once per surface, which only became correct with 8.5 — 45s of a sweep rather than 270
- [x] 8.7 Tests tying 8.5 and 8.6 together, since separating them captures an empty graph
- [ ] 8.8 **Measured and left alone:** the settle floor is 219s of a sweep, and shortening it weakens the
      check that catches a capture taken mid-resize
- [ ] 8.9 **Measured and left alone:** image conversion is 84ms a capture, 25s a sweep, already overlapped by workers
