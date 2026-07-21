# Code quality and editor setup

Practice Takes separates fast local formatting from slower repository-wide static analysis:

- `clang-format` rewrites C and C++ files to match `.clang-format` before every local commit.
- `clang-tidy` runs after relevant changes land on `main`, applies supported safe fixes, and commits those source changes back to `main`.
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

Install the Git hook from the repository root:

```bash
pre-commit install
```

Every commit runs `clang-format` against staged C and C++ files. When formatting changes a file, the commit stops so the result can be reviewed and staged. Run the commit again after staging the formatted files.

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
5. Runs `clang-tidy --fix` against the implementation files.
6. Runs `clang-format` over the resulting source and header edits.
7. Rebuilds and runs clang-tidy without fixes, failing when compilation or blocking findings remain.
8. Commits and pushes changed files under `src/` back to `main` as `github-actions[bot]`.

The workflow ignores pushes made by `github-actions[bot]`, preventing its own fix commit from starting another auto-fix cycle.

Automatic fixes use ordinary clang-tidy `--fix` behavior, not `--fix-errors`. Clang-tidy only applies replacements supplied by enabled checks; ambiguous or unsupported findings remain visible in the final verification step.

Repositories with branch protection must allow GitHub Actions to push the automatic fix commit to `main`. Otherwise the analysis can run, but the push step will fail.

## Manual clang-tidy use

Configure and build the project before running clang-tidy locally:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target PracticeTakes --parallel
```

The build step is required because JUCE generates `JuceHeader.h` during the build. A compilation database alone contains the include path but does not guarantee that the generated header exists.

Run without modifying files:

```bash
python scripts/quality/run_clang_tidy.py src/*.cpp
```

Run with supported fixes enabled:

```bash
python scripts/quality/run_clang_tidy.py --fix src/*.cpp
python scripts/quality/run_clang_format.py src/*.cpp src/*.h
```

Use a different build directory with:

```bash
CLANG_TIDY_BUILD_DIR=out/dev python scripts/quality/run_clang_tidy.py src/*.cpp
```

Set explicit executable paths when LLVM tools are not on `PATH`:

```bash
CLANG_FORMAT=/path/to/clang-format \
CLANG_TIDY=/path/to/clang-tidy \
python scripts/quality/run_clang_tidy.py --fix src/*.cpp
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
- `.github/workflows/clang-tidy-main.yml` fixes and verifies C++ after changes land on `main`.
- `scripts/quality/run_clang_format.py` locates and invokes clang-format.
- `scripts/quality/run_clang_tidy.py` locates and invokes clang-tidy with the build directory and optional safe fixes.
- `.vscode/settings.json` connects VS Code to CMake Tools and the compilation database.
