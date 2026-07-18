# Code quality and editor setup

Practice Takes uses three related tools:

- `clang-format` rewrites C and C++ files to match `.clang-format`.
- `clang-tidy` performs static analysis using `.clang-tidy` and CMake's real compiler commands.
- `pre-commit` runs both tools before a commit is created.

The checks are intentionally moderate. Analyzer, bug-prone, and performance findings block a commit. Readability findings are reported without turning every stylistic preference into an error.

## Install the tools

Install the following programs through the package manager appropriate for your platform:

- Python 3
- pre-commit
- LLVM/Clang, including `clang-format` and `clang-tidy`
- CMake

A portable way to install pre-commit after Python is available is:

```bash
python -m pip install pre-commit
```

## Configure the project before linting

`clang-tidy` needs `build/compile_commands.json`, which contains the exact include paths, generated JUCE headers, compiler definitions, and language settings used by CMake.

Configure the project once before installing or running the hooks:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
```

CMake is configured to export the compilation database automatically. The database is supported by Makefile and Ninja generators. On Windows, select Ninja in CMake Tools when the default Visual Studio generator does not create `compile_commands.json`.

The build directory can be changed for the lint hook:

```bash
CLANG_TIDY_BUILD_DIR=out/dev pre-commit run clang-tidy --all-files
```

## Enable the Git hook

From the repository root:

```bash
pre-commit install
```

After installation, each commit performs these steps:

1. `clang-format` formats staged C and C++ files.
2. `clang-tidy` analyzes staged implementation files under `src/` and applies fix-it replacements that the enabled check marks as safe.
3. Diagnostics without a supported fix remain visible and must be reviewed manually.

When either tool changes a file, pre-commit stops the commit so the result can be reviewed. Stage the changed files and run the commit again.

The hook uses clang-tidy's ordinary `--fix` mode rather than `--fix-errors`. It will not attempt to rewrite code when clang reports compilation errors.

## Run checks manually

Run every configured hook against the repository:

```bash
pre-commit run --all-files
```

Run only the formatter:

```bash
pre-commit run clang-format --all-files
```

Run clang-tidy with automatic safe fixes:

```bash
pre-commit run clang-tidy --all-files
```

Run clang-tidy directly without modifying files:

```bash
python scripts/run_clang_tidy.py src/*.cpp
```

Run it directly with supported fixes enabled:

```bash
python scripts/run_clang_tidy.py --fix src/*.cpp
```

Set explicit executable paths when LLVM tools are not on `PATH`:

```bash
CLANG_FORMAT=/path/to/clang-format \
CLANG_TIDY=/path/to/clang-tidy \
pre-commit run --all-files
```

## Resolving VS Code errors

The repository settings keep C/C++ error squiggles enabled. They configure the Microsoft C/C++ extension to obtain its configuration from CMake Tools and use `build/compile_commands.json` as a fallback.

After cloning or deleting the build directory:

1. Install the recommended **C/C++** and **CMake Tools** extensions.
2. Open the repository root in VS Code.
3. Run **CMake: Select a Kit** when prompted.
4. Run **CMake: Configure** from the Command Palette.
5. Wait for CMake Tools and IntelliSense indexing to finish.

This resolves the common false errors caused by VS Code not knowing about JUCE's generated `JuceHeader.h`, fetched JUCE sources, platform include directories, or CMake compile definitions.

When the project has been configured successfully but stale diagnostics remain, run **C/C++: Reset IntelliSense Database**, then reopen the affected file. Do not disable error squiggles; genuine syntax and type errors should remain visible.

## Tool configuration files

- `.clang-format` defines source formatting.
- `.clang-tidy` defines static-analysis checks.
- `.pre-commit-config.yaml` defines the commit hooks.
- `scripts/run_clang_format.py` locates and invokes `clang-format`.
- `scripts/run_clang_tidy.py` locates and invokes `clang-tidy` with the build directory and optional safe fixes.
- `.vscode/settings.json` connects VS Code to CMake Tools and the compilation database.
