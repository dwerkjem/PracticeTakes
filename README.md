# Practice Takes

Practice Takes is an early desktop music-practice application for Windows,
Linux, and macOS. It is currently focused on live microphone analysis and is
intended to grow into a more complete digital audio workstation over time.

> **Project status:** very early work in progress. Features, layouts, and file
> formats may change substantially.

## Current tools

### Tuner

The tuner listens to the selected microphone and displays:

- the detected note
- frequency and cents offset
- a pitch-history graph
- bar and meter display modes
- adjustable smoothing, averaging, note-switch, dropout, and graph-duration
  controls

### Spectrogram

The spectrogram displays a scrolling frequency view of the microphone input.
It is useful for examining harmonics, noise, resonance, and changes in tone
over time.

## Interface

The main window provides three top-level controls:

- **File** — reserved for future project and audio-file commands
- **Settings** — microphone selection and light/dark appearance
- **Tools** — opens the tuner and spectrogram

Each tool opens in its own resizable window. The tuner and spectrogram can
remain open at the same time and share the microphone selected in Settings.

When no usable microphone is available, the main window shows a dismissible
warning with a shortcut to Settings.

## Platforms

Automated packages are built for:

- Windows x64 and ARM64
- Linux x64 and ARM64
- macOS Intel and Apple Silicon

Published versions, when available, are provided through GitHub Releases as
native installers:

- Debian/Ubuntu `.deb` packages that install runtime dependencies through APT
  and add Practice Takes to the desktop Applications menu
- Windows `.exe` installers that bundle the required runtime libraries and add
  Practice Takes to the Start Menu
- macOS `.pkg` installers that place Practice Takes in `/Applications`

## Development

Developer documentation, including architecture, local builds, code
conventions, and release instructions, is kept in
[`docs/development/`](docs/development/README.md).

## License

Practice Takes is available under the permissive
[BSD 3-Clause License](LICENSE).
