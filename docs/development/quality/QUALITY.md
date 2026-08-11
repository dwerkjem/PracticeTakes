# Code quality and editor setup

Practice Takes separates fast local formatting from slower repository-wide static analysis:

- The SOPS secrets hook encrypts and stages configured secret mirrors before every local commit, and a companion audit hook rejects any tracked file that matches `tools/secret-patterns`.
- `clang-format` rewrites C and C++ files to match `.clang-format` before every local commit.
- `clang-tidy` runs after relevant changes land on `main`, applies supported safe fixes, and commits those source changes back to `main`.
- Pull requests run a check-only `clang-format`/`clang-tidy` gate across every `.cpp`/`.h` file under `src/`, failing the PR without modifying or committing anything.
- Sanitizers observe the test suite *running*, which static analysis cannot do. See [Runtime verification: sanitizers](#runtime-verification-sanitizers).
- VS Code uses CMake's compilation database, so editor diagnostics match the actual project configuration.

## Local pre-commit formatting

Local commits require:

- Python 3
- `pre-commit`
- `clang-format`

Install pre-commit with the package manager for your platform, or with Python:

```bash
python -m pip install pre-commit
```

Install the Git hooks from the repository root. Every clone needs this, because
the secret-protection hooks only run once they are installed:

```bash
pre-commit install
```

Every commit runs `clang-format` against staged C and C++ files. When formatting changes a file, the commit stops so the result can be reviewed and staged. Run the commit again after staging the formatted files.

The same hook run protects files selected by `tools/secret-patterns`. It removes
newly added plaintext secrets from the index and stages only their encrypted
mirrors below `.secrets/`. See [SOPS secret management](../operations/SECRETS.md) for setup,
synchronization, and conflict resolution.

Run the formatter manually across the repository with:

```bash
pre-commit run --all-files
```

or:

```bash
pre-commit run clang-format --all-files
```

Set an explicit executable when `clang-format` is not on `PATH`:

```bash
CLANG_FORMAT=/path/to/clang-format pre-commit run --all-files
```

## clang-format version

CI checks formatting with the clang-format on its runner, currently **18.1.8**.
Formatter versions disagree at the column limit — 21 accepts wrapping that 18
rejects — so a tree can be clean locally and red in CI.

Get the matching binary with:

```bash
uv sync --extra coverage
```

`tools/scripts/quality/run_clang_format.py` warns when the version in use is not the
pinned one, so the next drift is visible rather than mysterious. Set
`CLANG_FORMAT` to override which binary is used.

## Clang-tidy auto-fixes on main

Clang-tidy is not part of the local pre-commit hook. The `.github/workflows/clang-tidy-main.yml` workflow runs when relevant C++ or analysis configuration changes are pushed to `main`.

The workflow:

1. Checks out the updated `main` branch.
2. Installs Clang, CMake, Ninja, and the Linux JUCE development dependencies.
3. Configures `build/compile_commands.json`.
4. Builds `PracticeTakes` once so JUCE creates `JuceHeader.h` and other generated files required by the compiler commands.
5. Recursively discovers every `.cpp`/`.h` file under `src/` with `find` (all real source files live at least one directory below `src/`, so a plain `src/*.cpp` glob never matches) and fails loudly if discovery finds zero files.
6. Runs `clang-tidy --fix` against the discovered implementation files.
7. Runs `clang-format` over the resulting source and header edits.
8. Rebuilds and runs clang-tidy without fixes, failing when compilation or blocking findings remain.
9. Commits and pushes changed files under `src/` back to `main` as `github-actions[bot]`.

The workflow ignores pushes made by `github-actions[bot]`, preventing its own fix commit from starting another auto-fix cycle.

Clang-Tidy auto-fix and release runs share a FIFO queue. This serializes
auto-fix runs and ensures that a release requested after a relevant `main` push
does not select its source commit until the pending analysis and any automatic
fix commit have finished.

Automatic fixes use ordinary clang-tidy `--fix` behavior, not `--fix-errors`. Clang-tidy only applies replacements supplied by enabled checks; ambiguous or unsupported findings remain visible in the final verification step.

Repositories with branch protection must allow GitHub Actions to push the automatic fix commit to `main`. Otherwise the analysis can run, but the push step will fail.

## Pull request quality gate

The `.github/workflows/cpp-quality-check.yml` workflow runs on every pull request that touches `src/**` or the analysis configuration. Unlike the post-merge workflow, it never modifies or commits files - it only reports pass/fail.

The workflow:

1. Checks out the pull request.
2. Installs Clang, CMake, Ninja, and the Linux JUCE development dependencies.
3. Configures `build/compile_commands.json` and builds `PracticeTakes` once so JUCE generates `JuceHeader.h`.
4. Recursively discovers every `.cpp`/`.h` file under `src/` with `find`, the same way the post-merge workflow does, and fails loudly if discovery finds zero files.
5. Runs `clang-format --dry-run --Werror` against the discovered files, failing the check if any file is not already formatted.
6. Runs `clang-tidy` (without `--fix`) against the discovered `.cpp` files, failing the check if any finding falls in a `WarningsAsErrors` category (`clang-analyzer-*`, `bugprone-*`, `performance-*`).

This check analyzes the full repository on every run, not just changed lines, so it catches findings anywhere in `src/`, not only in the diff.

## Runtime verification: sanitizers

Everything above inspects source. Sanitizers instrument a build and watch it
run, which is the only way to catch a data race, a use-after-free, or an
allocation on the audio thread — none of which are visible in a green ordinary
test run.

One CMake cache variable selects the instrumentation:
`-DPRACTICE_TAKES_SANITIZE=address|thread|realtime`. It is single-valued on
purpose, so mutually incompatible sanitizers cannot be requested together by
construction rather than by a check. Each needs its own build tree: instrumented
objects are not interchangeable with ordinary ones, or with each other's.

| Leg                        | Workflow                    | Trigger                                | What runs             |
| -------------------------- | --------------------------- | -------------------------------------- | --------------------- |
| Address + Undefined        | `sanitizers.yml`            | pull requests touching `src/**`         | the full suite        |
| Realtime                   | `sanitizers.yml`            | pull requests touching `src/**`         | the `[callback]` cases |
| Thread                     | `sanitizers-scheduled.yml`  | nightly 04:20 UTC, and pushes to `main` | the `[.load]` cases   |

ThreadSanitizer is deliberately not a pull-request gate: it runs 5–15× slower
and `[.load]` is a soak. Running it on pushes to `main` as well as nightly keeps
a failure attributable to one commit instead of to a week of them.

**A failing scheduled run opens an issue** — `area:testing`, `area:audio`,
`type:bug`, `priority:p1` — naming the workflow, the run URL, and the failing
step. A later failure comments on that issue rather than opening a duplicate. A
red run that exists only in the Actions tab is a red run nobody reads.
`benchmarks.yml` and `secret-scan.yml` still have that gap; that is #159.

Leak findings from outside this repository are handled by `tools/sanitizers/lsan.supp`,
which carries a comment per entry explaining why. A leak from a file under
`src/` is covered by nothing and fails the check.

The Realtime leg is what enforces the audio-thread contract. What it does and
does not cover is in
[Audio-thread safety](../performance/audio-thread-safety.md).

Run any leg locally the same way CI does:

```bash
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
    -DPRACTICE_TAKES_SANITIZE=address
cmake --build build-asan --target PracticeTakesTests --parallel
ctest --test-dir build-asan --output-on-failure
```

`realtime` requires Clang 20 or newer and is rejected at configure time under
GCC, which has no equivalent.

## Manual clang-tidy use

Configure and build the project before running clang-tidy locally:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target PracticeTakes --parallel
```

The build step is required because JUCE generates `JuceHeader.h` during the build. A compilation database alone contains the include path but does not guarantee that the generated header exists.

Run without modifying files:

```bash
python tools/scripts/quality/run_clang_tidy.py $(find src -type f -name "*.cpp" -not -path "src/tests/*" -not -path "*/node_modules/*" | sort)
```

Run with supported fixes enabled:

```bash
python tools/scripts/quality/run_clang_tidy.py --fix $(find src -type f -name "*.cpp" -not -path "src/tests/*" -not -path "*/node_modules/*" | sort)
python tools/scripts/quality/run_clang_format.py $(find src -type f \( -name "*.cpp" -o -name "*.h" \) -not -path "src/tests/*" -not -path "*/node_modules/*" | sort)
```

Use a different build directory with:

```bash
CLANG_TIDY_BUILD_DIR=out/dev python tools/scripts/quality/run_clang_tidy.py $(find src -type f -name "*.cpp" -not -path "src/tests/*" -not -path "*/node_modules/*" | sort)
```

Set explicit executable paths when LLVM tools are not on `PATH`:

```bash
CLANG_FORMAT=/path/to/clang-format \
CLANG_TIDY=/path/to/clang-tidy \
python tools/scripts/quality/run_clang_tidy.py --fix $(find src -type f -name "*.cpp" -not -path "src/tests/*" -not -path "*/node_modules/*" | sort)
```

## Resolving VS Code errors

The repository settings keep C/C++ error squiggles enabled. They configure the Microsoft C/C++ extension to obtain its configuration from CMake Tools and use `build/compile_commands.json` as a fallback.

After cloning or deleting the build directory:

1. Install the recommended **C/C++** and **CMake Tools** extensions.
2. Open the repository root in VS Code.
3. Run **CMake: Select a Kit** when prompted.
4. Run **CMake: Configure** from the Command Palette.
5. Build the `PracticeTakes` target once so JUCE generates `JuceHeader.h`.
6. Wait for CMake Tools and IntelliSense indexing to finish.

This resolves common false errors caused by VS Code not knowing about JUCE's generated `JuceHeader.h`, fetched JUCE sources, platform include directories, or CMake compile definitions.

When configuration succeeds but stale diagnostics remain, run **C/C++: Reset IntelliSense Database**, then reopen the affected file. Do not disable error squiggles; genuine syntax and type errors should remain visible.

## Tool configuration files

- `.clang-format` defines source formatting.
- `.clang-tidy` defines static-analysis checks.
- `.pre-commit-config.yaml` runs clang-format before local commits.
- `tools/secret-patterns` selects plaintext files managed by the SOPS pre-commit hook.
- `tools/scripts/secrets/secrets_manager.py` encrypts, synchronizes, and resolves conflicts for secrets.
- `.github/workflows/clang-tidy-main.yml` fixes and verifies C++ after changes land on `main`.
- `.github/workflows/sanitizers.yml` runs the Address/Undefined and Realtime legs on pull requests.
- `.github/workflows/sanitizers-scheduled.yml` runs the Thread leg nightly and on `main`, and files the issue when it fails.
- `tools/sanitizers/lsan.supp` lists the third-party leaks LeakSanitizer ignores, with a reason each.
- `tools/scripts/quality/run_clang_format.py` locates and invokes clang-format.
- `tools/scripts/quality/run_clang_tidy.py` locates and invokes clang-tidy with the build directory and optional safe fixes.
- `.vscode/settings.json` connects VS Code to CMake Tools and the compilation database.
