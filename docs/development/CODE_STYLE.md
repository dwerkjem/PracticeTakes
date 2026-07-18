# C++ code style

Practice Takes favors straightforward C++ over compressed or overly clever
expressions. Code should be readable to someone who understands basic C++ and
is still learning JUCE or audio programming.

## General rules

- Use descriptive names that explain purpose, not just type.
- Keep functions focused on one stage of a workflow.
- Prefer early returns when they reduce nesting.
- Group class members by responsibility: UI, audio transport, analysis state,
  and rendering state.
- Put constants near the code that gives them meaning.
- Wrap long calls and conditions so related arguments remain visually grouped.
- Avoid abbreviations unless they are standard audio terms such as FFT, RMS,
  MIDI, or FIFO.

## Comments

Comments should explain intent, constraints, ownership, or non-obvious math.
They should not repeat syntax that is already clear from the code.

Good examples include:

- why work must stay off the real-time audio thread
- why a note-switch threshold exists
- why a logarithmic frequency scale is used
- why window destruction is deferred with `MessageManager::callAsync`

Avoid comments such as `// increment index` immediately above `++index`.

## Real-time audio rules

Code called directly by JUCE's audio callback must remain bounded and avoid:

- heap allocation
- locks or blocking waits
- file or network access
- logging
- UI updates
- pitch detection or FFT processing

The current tools copy samples into preallocated FIFOs. Message-thread timers
perform the expensive analysis and request repaints.

## Ownership

Use RAII and `std::unique_ptr` for windows and components with a single owner.
References are used for shared services, such as the global
`AudioDeviceManager`, whose lifetime is guaranteed by `MainComponent`.

## Formatting

The repository does not require a strict linter. Preserve the existing style:

- four-space indentation
- braces on the following line
- spaces inside control-flow conditions only where conventional
- one declaration per line when state has different responsibilities
- line lengths kept reasonable through deliberate wrapping
