# Contributing

Practice Takes is maintained by one developer, so process is kept light. The
full guide is
[`CONTRIBUTING.md`](https://github.com/dwerkjem/PracticeTakes/blob/main/.github/CONTRIBUTING.md);
this is the short version.

## Building it

It is a C++20 application on JUCE 8, built with CMake, with one Rust crate (the
search index, built by CMake too), so it needs a Rust toolchain from
[rustup](https://rustup.rs) as well as a C++ compiler. On Linux:

```bash
./tools/scripts/build/build-and-run.sh                 # configure, build, run
./tools/scripts/build/build-and-run.sh --build-only
./tools/scripts/build/build-and-run.sh --install-dependencies
```

Anywhere else:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target PracticeTakes --parallel
```

Prerequisites for each platform are in
[`BUILDING.md`](https://github.com/dwerkjem/PracticeTakes/blob/main/docs/development/build/BUILDING.md).

## Testing it

| Change under | Run |
|---|---|
| `src/**` (C++) | `cmake -S . -B build -DBUILD_TESTING=ON && cmake --build build --target PracticeTakesTests && ctest --test-dir build --output-on-failure` |
| `src/search/**` (the search index, Rust) | `cargo test` and `cargo clippy` from `src/search/`; the C++ tests above search through it too |
| `src/services/**` (the feedback worker, TypeScript) | `npm run check && npm run test` from `src/services/` |
| `tools/scripts/**` (Python tooling) | `python tools/scripts/run_tests.py` |

`uv run test-suite` opens a hub that runs every suite the project has, including
screenshot capture and review. See
[`TESTING_SUITE.md`](https://github.com/dwerkjem/PracticeTakes/blob/main/docs/development/quality/TESTING_SUITE.md).

## How work flows

- **Small, single-layer changes** that follow an existing pattern go straight to
  a pull request.
- **Anything larger** goes through an [OpenSpec](https://github.com/dwerkjem/PracticeTakes/tree/main/openspec)
  proposal first: a change across layers, a new shared service, or anything
  touching the audio-thread contract. At most three are in implementation at
  once.
- **Issues** are labelled `area:`, `type:` and `priority:`, plus a triage
  `status:`. `status:ready-for-agent` means an AI agent may pick it up;
  `status:ready-for-human` means it needs hands, such as real hardware.
- **Roadmap issues** sit beneath a stage tracker and carry a size in
  [the ledger](https://github.com/dwerkjem/PracticeTakes/blob/main/tools/scripts/planning/roadmap_ledger.json).
  A new one gets its size in the same pull request that schedules it.
- **Close finished work as *completed*** and dropped work as *not planned*. The
  roadmap forecast credits only the first.

## Two rules that are never bent

1. **The audio callback stays bounded.** No allocation, locks, blocking, I/O or
   analysis in it. A RealtimeSanitizer job enforces this on every pull request.
2. **The repository root stays minimal.** Source goes under `src/`, tooling under
   `tools/`, documentation under `docs/`.

See [[Architecture at a Glance]] for why.
