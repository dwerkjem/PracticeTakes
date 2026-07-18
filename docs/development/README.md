# Practice Takes development

This directory contains contributor and maintainer documentation. The root
`README.md` is intentionally written for application users.

Practice Takes is in early development. The current codebase provides a JUCE
desktop shell, a tuner, and a spectrogram. The longer-term direction is a
broader music-practice and digital-audio workstation application.

## Documentation

- [Building](BUILDING.md) — prerequisites, local configuration, and run commands
- [Architecture](ARCHITECTURE.md) — application ownership, audio flow, and UI structure
- [Code style](CODE_STYLE.md) — readability and real-time audio guidelines
- [Code quality](QUALITY.md) — clang-format, clang-tidy, pre-commit, and VS Code diagnostics
- [Releasing](RELEASING.md) — semantic versions and GitHub release automation

## Main technologies

- C++20
- JUCE 8
- CMake 3.25 or newer
- vcpkg for Linux system dependencies
- GitHub Actions for Windows, Linux, and macOS packages

## Source layout

```text
src/
├── main.cpp                    Application entry point and main window
├── MainComponent.cpp/.h        Main shell, settings, tool windows, and warnings
├── TunerComponent.cpp/.h       Pitch detection, smoothing, controls, and rendering
└── SpectrogramComponent.cpp/.h FFT processing and scrolling frequency display
```

The application version is stored only in the root `VERSION` file. Do not
copy the version into CMake or C++ source code.
