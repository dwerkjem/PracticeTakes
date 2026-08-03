## 1. Remove the application code

- [x] 1.1 Delete `src/application/shell/ui/performance/PerformanceLabWindow.h` and
  the `MainComponent` hooks that open it
- [x] 1.2 Delete the benchmarking engine under `src/features/performance/`,
  keeping `ApplicationLaunchTimer`
- [x] 1.3 Delete their tests and any fixture left with no user
- [x] 1.4 Remove `PRACTICE_TAKES_ENABLE_PERFORMANCE_LAB` and the source entries
  from `CMakeLists.txt`

## 2. Remove the tooling and documentation

- [x] 2.1 Delete `tools/scripts/quality/run-performance-lab.sh` and its mention
  in `tools/scripts/README.md`
- [x] 2.2 Delete `docs/development/performance/hardware-acceptance.md` and index
  entries for it
- [x] 2.3 Update `docs/development/agents/AGENT_GUIDE.md`
- [x] 2.4 Say in the testing suite's documentation where measurements come from
  now

## 3. Verify

- [x] 3.1 `PracticeTakes` and `PracticeTakesTests` build with no reference left
- [x] 3.2 `ctest` passes, and the benchmark cases still run through the suite
- [x] 3.3 `openspec validate --all --strict` passes with the capability removed
