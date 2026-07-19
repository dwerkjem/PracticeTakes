# Building Practice Takes

## Requirements

Practice Takes requires:

- CMake 3.25 or newer
- a C++20 compiler
- Git
- platform audio and window-system development libraries

JUCE is downloaded by CMake at configure time. Linux's dependencies are managed
through the repository's `vcpkg.json` manifest and custom triplets.

## Linux helper script

The simplest supported local workflow on Linux is:

```bash
./scripts/build-and-run.sh
```

Useful options:

```bash
./scripts/build-and-run.sh --build-only
./scripts/build-and-run.sh --clean
BUILD_TYPE=Release ./scripts/build-and-run.sh
./scripts/build-and-run.sh --install-dependencies
./scripts/build-and-run.sh --jobs 2
```

On Debian and Ubuntu, the script first checks every required system package.
If anything is missing, it lists the packages and asks before using `apt-get`.
It never installs packages without an interactive confirmation or the explicit
`--install-dependencies` option. Other Linux distributions receive a clear
manual-installation message.

Use `--jobs N` to limit the number of concurrent compiler processes. This is
useful on laptops where an unrestricted parallel build can exhaust memory and
cause VS Code or other applications to close. The same value can be supplied
through the `BUILD_JOBS` environment variable.

The script detects vcpkg through `VCPKG_ROOT` or the `vcpkg` executable,
selects the repository's architecture-specific Linux triplet, configures
CMake, builds `PracticeTakes`, and launches the executable unless
`--build-only` was supplied.

## Generic CMake workflow

A basic single-configuration build is:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target PracticeTakes --parallel
```

Multi-configuration generators, including Visual Studio and Xcode, select the
configuration during the build step:

```bash
cmake -S . -B build
cmake --build build --config Debug --target PracticeTakes --parallel
```

## Compilation database

The project enables `CMAKE_EXPORT_COMPILE_COMMANDS`, producing
`build/compile_commands.json` when the selected CMake generator supports it.
That database contains the exact compiler commands used for each source file
and is consumed by `clang-tidy` and VS Code.

Makefile and Ninja generators produce the database. IDE generators such as
Visual Studio and Xcode do not. Use Ninja for a dedicated linting build tree
when necessary:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

Configuring is enough to create the database; the executable does not need to
be built first. See [Code quality and editor setup](QUALITY.md) for pre-commit
and VS Code instructions.

## Build output

CMake places the executable in `build/bin` for ordinary desktop builds. macOS
uses the JUCE-generated application bundle layout.

## Version source

CMake reads the root `VERSION` file and rejects values that do not use the
`MAJOR.MINOR.PATCH` format. JUCE then places that same version in application
metadata and the visible window title.

## Continuous integration

The pull-request workflow builds and packages six targets:

- Windows x64
- Windows ARM64
- Linux x64
- Linux ARM64
- macOS Intel x64
- macOS Apple Silicon ARM64

A successful CI build confirms compilation and packaging. It does not replace
interactive testing of audio devices, window behavior, or platform appearance.
