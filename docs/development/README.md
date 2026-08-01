# Practice Takes development

This directory contains contributor and maintainer documentation. The root
`README.md` is intentionally written for application users.

Practice Takes is in early development. The current codebase provides a JUCE
desktop shell, a tuner, and a spectrogram. The longer-term direction is a
broader music-practice and digital-audio workstation application.

## Documentation

- [Architecture map](https://dwerkjem.github.io/PracticeTakes/) — browsable
  graph of every file, its role, and what it connects to, with a guided tour
  through the codebase. Generated from the repository; no setup required
- [Building](BUILDING.md) — prerequisites, local configuration, and run commands
- [Architecture](ARCHITECTURE.md) — application ownership, audio flow, and UI structure
- [Design and architecture review checklist](ARCHITECTURE_QA.md) — what reviewers check on pull requests beyond formatting/linting
- [QA strategy](QA_STRATEGY.md) — the current CI/testing gap analysis and plan for closing it
- [Test layout](TEST_LAYOUT.md) — how `tests/` mirrors `src/`, where a new test goes, and what sits outside the mirror
- [Manual GUI verification](MANUAL_GUI_VERIFICATION.md) — the interactive harness that drives the app, asks about each surface, and records the run
- [Code style](CODE_STYLE.md) — readability and real-time audio guidelines
- [Code quality](QUALITY.md) — clang-format, clang-tidy, pre-commit, and VS Code diagnostics
- [SOPS secrets](SECRETS.md) — encrypted secret mirrors, synchronization, and conflict resolution
- [Releasing](RELEASING.md) — semantic versions, GitHub release automation, and
  artifact signing

## Main technologies

- C++20
- JUCE 8
- CMake 3.25 or newer
- vcpkg for Linux system dependencies
- GitHub Actions for Windows, Linux, and macOS packages

The application version is stored only in the root `VERSION` file. Do not
copy the version into CMake or C++ source code.
