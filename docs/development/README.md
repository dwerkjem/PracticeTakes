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

### `build/`

- [Building](build/BUILDING.md) — prerequisites, local configuration, and run commands

### `architecture/`

- [Architecture](architecture/ARCHITECTURE.md) — application ownership, audio flow, and UI structure
- [Design and architecture review checklist](architecture/ARCHITECTURE_QA.md) — what reviewers check on pull requests beyond formatting/linting

### `quality/`

- [Code quality](quality/QUALITY.md) — clang-format, clang-tidy, pre-commit, and VS Code diagnostics
- [Code style](quality/CODE_STYLE.md) — readability and real-time audio guidelines
- [QA strategy](quality/QA_STRATEGY.md) — the current CI/testing gap analysis and plan for closing it

### `performance/`

- [Audio-thread safety](performance/audio-thread-safety.md) — the real-time capture and telemetry contract
- [Hardware acceptance](performance/hardware-acceptance.md) — the physical-hardware evidence protocol for the Performance Lab

### `operations/`

- [Releasing](operations/RELEASING.md) — semantic versions, GitHub release automation, and
  artifact signing
- [SOPS secrets](operations/SECRETS.md) — encrypted secret mirrors, synchronization, and conflict resolution
- [Feedback service](operations/FEEDBACK.md) — the in-app feedback contract and endpoint configuration

### `agents/`

- [Agent guide](agents/AGENT_GUIDE.md) — repository guidance loaded by Claude Code
  through the root `CLAUDE.md` stub

## Main technologies

- C++20
- JUCE 8
- CMake 3.25 or newer
- vcpkg for Linux system dependencies
- GitHub Actions for Windows, Linux, and macOS packages

The application version is stored only in `tools/VERSION`. Do not copy the
version into CMake or C++ source code.
