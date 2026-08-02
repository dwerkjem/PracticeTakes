# Code quality and editor setup

Practice Takes separates fast local formatting from slower repository-wide static analysis:

- The SOPS secrets hook encrypts and stages configured secret mirrors before every local commit, and a companion audit hook rejects any tracked file that matches `tools/secret-patterns`.
- `clang-format` rewrites C and C++ files to match `.clang-format` before every local commit.
- `clang-tidy` runs after relevant changes land on `main`, applies supported safe fixes, and commits those source changes back to `main`.
- Pull requests run a check-only `clang-format`/`clang-tidy` gate across every `.cpp`/`.h` file under `src/`, failing the PR without modifying or committing anything.
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
mirrors below `.secrets/`. See [SOPS secret management](SECRETS.md) for setup,
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
- `tools/scripts/quality/run_clang_format.py` locates and invokes clang-format.
- `tools/scripts/quality/run_clang_tidy.py` locates and invokes clang-tidy with the build directory and optional safe fixes.
- `.vscode/settings.json` connects VS Code to CMake Tools and the compilation database.
