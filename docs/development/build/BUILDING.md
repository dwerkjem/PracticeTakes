# Building Practice Takes

## Requirements

Practice Takes requires:

- CMake 3.25 or newer
- a C++20 compiler
- Git
- platform audio and window-system development libraries

JUCE is downloaded by CMake at configure time. Linux's dependencies are managed
through the repository's `tools/vcpkg.json` manifest and custom triplets.

## Linux helper script

The simplest supported local workflow on Linux is:

```bash
./tools/scripts/build/build-and-run.sh
```

Useful options:

```bash
./tools/scripts/build/build-and-run.sh --build-only
./tools/scripts/build/build-and-run.sh --clean
BUILD_TYPE=Release ./tools/scripts/build/build-and-run.sh
./tools/scripts/build/build-and-run.sh --install-dependencies
./tools/scripts/build/build-and-run.sh --jobs 2
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

On a Linux machine where Nix is installed alongside the distribution packages,
run these commands with the system toolchain rather than whichever one is first
on `PATH`:

```bash
/usr/bin/cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=/usr/bin/gcc -DCMAKE_CXX_COMPILER=/usr/bin/g++
```

Nix builds CMake with an empty `CMAKE_SYSTEM_PREFIX_PATH` so that packages
cannot pick up host libraries, which also hides `/usr` from `find_package` and
makes `find_package(X11)` fail. Dependencies located through `pkg-config` are
unaffected, so X11 is usually the only thing that breaks. Configuring with the
wrong toolchain also rewrites `build/CMakeCache.txt`, which forces the next
`build-and-run.sh` run to reconfigure from scratch and surface the failure
there. `tools/scripts/build/build-and-run.sh` selects the system toolchain for you.

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
be built first. See [Code quality and editor setup](../quality/QUALITY.md) for pre-commit
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

The platform-independent unit test suite runs once on Linux x64. Every other
matrix job disables the test target so it can concentrate on architecture
compilation and native packaging without rebuilding Catch2 five more times.
A successful CI build confirms compilation, unit tests, and packaging. It does
not replace interactive testing of audio devices, window behavior, or platform
appearance.

## Native installers

The shared build workflow creates a native installer for each architecture:

- CPack's DEB generator creates Debian `.deb` packages on Debian 12, the oldest
  supported Linux release. Building on that compatibility baseline prevents the
  package from inheriting newer glibc, libstdc++, or `t64` package requirements.
  It runs
  `dpkg-shlibdeps` against the finished executable so the package's `Depends`
  field is derived from the libraries that binary actually uses. The package
  also installs the desktop entry under `/usr/share/applications`. The package
  is installed inside the build container, then tested again on Debian 12,
  Debian 13, Ubuntu 22.04, and Ubuntu 24.04 for both x64 and ARM64. These checks
  verify APT dependency resolution and ensure every linked runtime library is
  present.
- CPack's NSIS generator creates Windows `.exe` installers. CMake includes the
  required MSVC runtime libraries, and the installer creates a Practice Takes
  Start Menu shortcut.
- Apple's `pkgbuild` creates macOS `.pkg` installers that install the JUCE
  application bundle in `/Applications`.

The package file name can be controlled during configuration without changing
the application version:

```bash
cmake -S . -B build \
  -DPRACTICE_TAKES_PACKAGE_FILE_NAME=PracticeTakes-0.4.0-linux-x64
cmake --build build --config Release
cpack --config build/CPackConfig.cmake -G DEB -B dist
```

Ordinary development builds do not need to run CPack.
