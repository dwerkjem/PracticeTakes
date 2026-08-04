# Test layout

`src/tests/` mirrors `src/`, so a test's path names the source directory it covers.

```
src/services/score/TempoMap.h
src/tests/platform/score/TempoMapTests.cpp
```

The tree was flat until July 2026. With forty-odd files in one directory the
only clue about what a test covered was its filename, and nothing tied a test to
a layer — you could not answer "what covers the audio service?" by looking.

## Where a new test goes

Beside the mirror of the file it exercises. If `src/` gains a directory, create
the matching one under `src/tests/` rather than putting the test at the top level.

Add the file to `add_executable(PracticeTakesTests ...)` in `CMakeLists.txt`;
the source list is explicit, so a test not listed there silently never runs.

## Includes

Both `src/` and `src/tests/` are on the test target's include path, so:

- a test includes its subject by source-relative path —
  `#include "src/services/score/TempoMap.h"`
- a shared fixture is included as `#include "support/BenchmarkFakes.h"`,
  from any depth

Neither needs a relative path, so moving a test between directories does not
break its includes.

## What sits outside the mirror

`src/tests/support/` holds fixtures used by tests in more than one area. Those
belong to no single source directory, so there is nothing for them to mirror.

Suites that do not correspond to a single source file — the end-to-end smoke
suite and the load suite — are also outside the mirror and get their own
top-level directory.

Anything outside the mirror must be listed in `NON_MIRRORED_DIRECTORIES` in
`tools/scripts/quality/check_test_layout.py`, with the reason. That list is the
documentation of what is exempt; keeping it short is the point.

## The check

`tools/scripts/quality/check_test_layout.py` enforces two rules and runs in CI on any
change to `src/tests/`, `src/`, or `tools/scripts/`:

1. No test file sits directly at the root of `src/tests/`. The root is where files
   land when nobody decided where they belong.
2. Every directory holding tests mirrors a directory that exists under `src/`,
   unless it is explicitly exempt. A mirrored path with no counterpart usually
   means the source moved and the test did not, leaving the test covering
   something by a name that no longer describes it.

Run it directly with:

```bash
python3 tools/scripts/quality/check_test_layout.py
```

It reports every violation in one run rather than stopping at the first, and
exits non-zero when the tree does not conform.
