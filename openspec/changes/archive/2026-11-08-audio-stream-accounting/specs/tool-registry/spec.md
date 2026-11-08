## MODIFIED Requirements

### Requirement: Microphone tool instances are capped by audio consumer capacity
The shell SHALL NOT create more than six instances of any one multi-instance
tool, and SHALL NOT create an instance of a tool that reads audio samples unless
the shared audio input service has a free sample stream. A tool that needs only
the input's state (started, stopped, muted, disconnected, clipping) and not its
samples SHALL register as a state-only listener, which takes no sample stream
and is never visited by the audio callback; the tuner, which reads its pitch
from the shared pitch analysis, SHALL be such a listener. The service SHALL have
sixteen preallocated sample streams.

"Open another" SHALL say why it cannot open: when the tool's six instances are
open it SHALL be disabled and say that six is the most; when no sample stream is
free it SHALL say so, and choosing it SHALL explain which tools, and how many
of each, are using the streams. Restoring a workspace SHALL drop instances past
the sixth of a tool, SHALL open sample-reading instances in the order the shell
lists them while streams remain, and SHALL leave the rest closed and out of the
tiling tree, without failing the restore. The audio callback's bounds SHALL be
unchanged.

#### Scenario: Tuners take no audio stream
- **WHEN** any number of tuners, up to six, are open
- **THEN** the number of free sample streams is the same as with none open

#### Scenario: The menu says why another copy can't open
- **WHEN** six copies of a tool are open and the user opens its Tools submenu
- **THEN** "Open another" is disabled and its text says six is the most

#### Scenario: Running out of streams says who has them
- **WHEN** no sample stream is free and the user chooses "Open another" for a
  spectrogram
- **THEN** no instance is created, and an alert says how many streams there are
  and how many spectrograms, harmonic analyzers and other users hold them

#### Scenario: An oversized workspace restores what fits
- **WHEN** a workspace names a seventh copy of a tool, or more sample-reading
  instances than there are free streams
- **THEN** the seventh copy is dropped, instances open until the streams are
  used, the rest stay closed, and the workspace is otherwise restored
