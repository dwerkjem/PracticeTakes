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
separately for every tool. `AudioInputService` owns the one hardware callback;
the tuner and spectrogram register as consumers of that service while their
windows are open.

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

JUCE calls `AudioInputService::audioDeviceIOCallbackWithContext` on the
real-time audio thread. That callback performs only bounded work:

1. clear any requested output buffers
2. read the atomic mute and input-gain controls
3. measure the post-gain peak
4. copy microphone samples into one preallocated SPSC FIFO per active tool
5. return without allocation, locks, logging, file access, analysis, or UI code

Each tool drains only its own FIFO from its message-thread timer and performs
analysis there. A slow tool therefore cannot block capture or prevent another
tool from receiving samples.

Each consumer FIFO holds 65,536 mono samples. If a complete device-callback
block does not fit, that newest block is dropped for only that consumer;
already-buffered samples are preserved in order. Dropped blocks and samples
are counted with lock-free atomics and reported in Settings outside the audio
callback.

Device start/stop and sample-rate or active-input-channel changes are stored
atomically by the device callbacks and delivered to consumers on the service
timer. Consumers discard pending samples when the format changes, preventing
frames from different formats from being analyzed together.

The device-running callbacks, rather than the backend's active-channel bitset,
define whether an input is usable. This matters on ALSA, where an open device
can deliver input while reporting an empty or mismatched channel mask. Healthy
devices are rescanned every 15 seconds; disconnected devices are rescanned
every 2 seconds. Recovery never replaces a backend that still reports itself
open, avoiding a race with its capture thread.

The Settings input-volume control applies a shared 0–200% software gain before
fan-out. The live level meter displays the post-gain peak, and the clipping
state is held briefly so it remains visible without requiring UI work in the
callback.

## Tuner pipeline

The tuner:

1. copies recent microphone samples into a fixed analysis window
2. calculates RMS input level
3. estimates pitch using zero-padded FFT normalized autocorrelation
4. converts frequency to a fractional MIDI-note value
5. averages and eases the result to reduce visual jitter
6. applies note-switch hysteresis, so the displayed note does not chatter
7. stores recent values for the history graph

The FFT reduces each analysis frame from quadratic to `O(N log N)` work while
preserving the tuner's original peak selection and normalization. Its scratch
buffers are preallocated, and the timer skips analysis when no new microphone
samples have arrived.

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
