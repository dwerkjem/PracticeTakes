# Practice Takes architecture

## Application ownership

`PracticeTakesApplication` owns the main `DocumentWindow`. The main window owns
one `MainComponent`, which acts as the application shell and owns shared global
services.

`MainComponent` owns:

- one `AudioDeviceManager`
- the application `LookAndFeel`
- the Settings window
- one optional window for each open tool
- the nonmodal microphone warning card

Keeping one shared `AudioDeviceManager` avoids opening the same microphone
separately for every tool. The tuner and spectrogram register independent audio
callbacks with that manager while their windows are open.

## Main window

The main window intentionally remains small and uncluttered. Its top buttons
provide:

- `File`, currently reserved for future commands
- `Settings`, which opens global appearance and audio-device controls
- `Tools`, which opens individual analysis windows

Tools are not embedded inside the main window. Each tool owns a resizable
`DocumentWindow`, allowing the tuner and spectrogram to remain visible at the
same time.

## Theme propagation

The light/dark choice is stored by `MainComponent`. When it changes,
`MainComponent` updates the shared JUCE look-and-feel and then notifies every
open settings or tool window. Tool components also maintain their own small
palette because their custom graphics are drawn directly rather than entirely
through JUCE controls.

## Audio-thread boundary

JUCE calls each tool's `audioDeviceIOCallbackWithContext` on the real-time
audio thread. That callback performs only bounded work:

1. clear any requested output buffers
2. copy microphone samples into a preallocated FIFO
3. return without running pitch detection, FFT work, or UI code

A timer on the message thread drains the FIFO and performs analysis. This
keeps expensive calculations and painting away from the real-time audio
callback.

## Tuner pipeline

The tuner:

1. copies recent microphone samples into a fixed analysis window
2. calculates RMS input level
3. estimates pitch using normalized autocorrelation
4. converts frequency to a fractional MIDI-note value
5. averages and eases the result to reduce visual jitter
6. applies note-switch hysteresis, so the displayed note does not chatter
7. stores recent values for the history graph

The user can display the result as a history graph, horizontal bar, or meter.

## Spectrogram pipeline

The spectrogram:

1. reads one FFT-sized block from its FIFO
2. applies a Hann window
3. performs a frequency-only FFT
4. maps magnitudes to decibels
5. draws a new logarithmically spaced frequency column
6. shifts the existing image left to create a scrolling display

The rendered color mapping differs between light and dark themes, so the graph
remains legible in either appearance.
