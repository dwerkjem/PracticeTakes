# tool-registry Specification

## Purpose

Defines the single contract every analysis tool is declared and built through, so
that adding a tool is one registry entry and no change to the application shell.
Covers what a registry entry carries, how a tool's stable identity differs from
one live instance of it, the instance policy the shell enforces on its behalf,
the shared services a tool is handed and may not outlive, how per-instance
settings are serialised, and the order its lifecycle runs in. The rule this
exists to hold: no part of the shell names an individual tool.

## Requirements
### Requirement: Tools are declared in one registry entry
The application SHALL discover every analysis tool through a registry of
declarative entries. A registry entry SHALL carry the tool's stable identifier,
its user-facing display name, any historical alias identifiers, a factory that
constructs the tool's component, a preferred floating-window size, an instance
policy, and an optional per-instance settings codec with a settings version.
Registering an entry SHALL be sufficient to make a tool appear in the Tools
menu, be openable docked or floating, participate in tiling and tab groups, be
captured into and restored from a workspace, and receive appearance changes. No
other part of the application shell SHALL name an individual tool.

#### Scenario: A new tool needs only a registry entry
- **WHEN** a developer adds a registry entry for a tool that the shell has never
  referenced by name
- **THEN** that tool is offered in the Tools menu, opens both docked and
  floating, tiles and tabs alongside existing tools, and is captured into and
  restored from workspaces, with no edit to the shell's presentation, menu,
  snapshot, appearance, or layout code

#### Scenario: The shell resolves tools only through the registry
- **WHEN** the shell needs a tool's display name, preferred size, component, or
  settings codec
- **THEN** it obtains them from that tool's registry entry rather than from a
  branch that tests the tool's identity

### Requirement: Tool identity and instance identity are distinct
The application SHALL distinguish a tool identifier, which names a kind of tool,
from an instance identifier, which names one live copy of a tool. Every live
tool instance SHALL carry both. Workspace topology, floating bounds, focus, and
per-instance settings SHALL be keyed by instance identifier. The registry SHALL
be keyed by tool identifier.

#### Scenario: An instance resolves to its declaring tool
- **WHEN** the shell holds an instance identifier
- **THEN** it can resolve the tool identifier that instance was created from,
  and through it the tool's registry entry

#### Scenario: Instance identifiers are what workspaces record
- **WHEN** a workspace snapshot is captured
- **THEN** its dock topology, tab groups, floating bounds, focused entry, and
  per-instance settings are keyed by instance identifier

### Requirement: Each tool declares an instance policy that the shell enforces
A registry entry SHALL declare whether its tool is single-instance or
multi-instance. For a single-instance tool the shell SHALL reuse the existing
instance when the tool is opened again, focusing it rather than creating a
second one. For a multi-instance tool, "open or focus" SHALL focus its first
live instance, or open one when none is live, and only an explicit request for
another instance SHALL create an additional instance with its own instance
identifier. The tuner, the spectrogram and the harmonic analyzer SHALL be
multi-instance. The score view and the target note tool SHALL be
single-instance.

#### Scenario: Opening an already-open single-instance tool focuses it
- **WHEN** the user opens a single-instance tool that already has a live
  instance
- **THEN** the existing instance is focused and no second instance is created

#### Scenario: A single-instance tool refuses a duplicate instance
- **WHEN** something requests a second instance of a single-instance tool
- **THEN** the request is refused and the shell's set of live instances is
  unchanged

#### Scenario: Restoring a workspace naming one tool twice does not duplicate it
- **WHEN** a workspace snapshot places two instances of the same
  single-instance tool
- **THEN** restoration keeps at most one instance of that tool and the
  workspace still restores successfully

#### Scenario: Open or focus does not multiply a multi-instance tool
- **WHEN** the user chooses "Open or focus" for a multi-instance tool that
  already has a live instance
- **THEN** that instance is focused and no instance is created

### Requirement: Tools receive shared services and cannot outlive them
The registry factory SHALL receive one shared-service bundle giving a tool
access to audio input, appearance and theme, and settings. A tool SHALL obtain
these services only through the bundle and SHALL NOT reach into the application
shell for them. Every live tool instance SHALL be destroyed before the services
in the bundle are destroyed.

#### Scenario: A tool is constructed with the service bundle
- **WHEN** the shell creates a tool instance from its registry entry
- **THEN** the factory is given the shared-service bundle and the constructed
  component uses those services for audio, appearance, and settings

#### Scenario: Shutdown destroys instances before services
- **WHEN** the application shuts down with tool instances still open
- **THEN** every tool instance is destroyed before the audio, appearance, and
  settings services it referenced are destroyed

### Requirement: Per-instance settings are serialised through the registry
A registry entry MAY declare a settings codec that converts one instance's
settings to and from a versioned payload. When a workspace is captured, the
shell SHALL ask each live instance's codec for its payload and store it under
that instance's identifier. When a workspace is restored, the shell SHALL give
each instance the payload stored under its identifier before that instance
becomes visible. A payload whose version does not match the entry's declared
settings version SHALL be discarded and the instance SHALL start from its
defaults.

#### Scenario: Instance settings survive capture and restore
- **WHEN** a tool with a settings codec has non-default settings and its
  workspace is captured and later restored
- **THEN** the restored instance uses the captured settings

#### Scenario: A tool without a codec restores to defaults
- **WHEN** a tool that declares no settings codec is captured and restored
- **THEN** the restored instance starts from its defaults and no settings
  payload is stored for it

#### Scenario: A stale settings payload is discarded
- **WHEN** a stored payload's version differs from the tool's declared settings
  version
- **THEN** the payload is discarded and the instance starts from its defaults

### Requirement: The tool lifecycle is ordered and observable
The shell SHALL take a tool instance through an ordered lifecycle: created from
its factory, given any restored settings, attached to a docked or floating
presentation, optionally moved between presentations, detached, and destroyed.
Moving an instance between docked and floating presentations SHALL detach and
reattach it without destroying it, so its analysis state and audio registration
survive the move. Closing an instance SHALL capture its settings payload before
destruction.

#### Scenario: Moving between presentations preserves the instance
- **WHEN** an open tool instance is moved from docked to floating, or the
  reverse
- **THEN** the same component object is reattached to the new presentation and
  its analysis state and audio registration are unchanged

#### Scenario: Closing captures settings before destruction
- **WHEN** an open tool instance with a settings codec is closed
- **THEN** its settings payload is captured into the active workspace before the
  component is destroyed

#### Scenario: Lifecycle order is verifiable without a display
- **WHEN** the lifecycle is exercised in a test
- **THEN** the sequence of create, restore-settings, attach, detach, capture-
  settings, and destroy events is observable and asserted without constructing
  a JUCE window

### Requirement: The user can open another instance of a multi-instance tool
The Tools menu SHALL offer "Open another" for every multi-instance tool. Choosing
it SHALL create an instance with the lowest ordinal that is not live, give it
its own settings, analysis and audio consumer, and present it without changing
any other instance. While a multi-instance tool has two or more live instances,
its Tools submenu SHALL list each of them by name, with focus, dock, float,
rename and close actions aimed at that instance. While it has one or none, the
submenu SHALL keep its single-instance form, with "Open another" added.

#### Scenario: Two tuners run side by side
- **WHEN** the user opens the tuner and then chooses "Open another" for it
- **THEN** two tuner instances are live, each with its own settings and its own
  pitch reading, and changing a setting in one leaves the other unchanged

#### Scenario: A closed instance's ordinal is reused
- **WHEN** three tuners are open, the second is closed, and the user opens
  another
- **THEN** the new instance takes the second instance's ordinal rather than a
  fourth

#### Scenario: Each live instance is reachable from the menu
- **WHEN** two spectrograms are open
- **THEN** the Spectrogram submenu lists both by name, and closing one from its
  entry leaves the other open

### Requirement: Instances are named, and can be renamed
Every live instance SHALL have a display name. The name SHALL be the user's label
for that instance when one is set, and otherwise the tool's display name for
the first instance, and the display name followed by the ordinal after that. The
user SHALL be able to rename an instance from its Tools menu entry and from its
own options menu. A label SHALL be
trimmed, stripped of control characters, and at most 40 characters long. An
empty label SHALL restore the default name. The name SHALL appear in the
instance's floating window title, its tab label, its docked panel header and
its menu entry, and SHALL update in all of them when renamed, without
recreating the instance.

#### Scenario: Default names tell instances apart
- **WHEN** two harmonic analyzers are open and neither has been renamed
- **THEN** they are shown as "Harmonic Analyzer" and "Harmonic Analyzer 2"

#### Scenario: A rename shows everywhere at once
- **WHEN** the user renames "Tuner 2" to "Violin" while it is floating
- **THEN** its window title and its menu entry read "Violin", and its analysis
  continues uninterrupted

#### Scenario: Clearing a label restores the default
- **WHEN** the user renames an instance to an empty name
- **THEN** it is shown under its default name again

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

### Requirement: Several instances of one tool survive save and restore
Workspace capture SHALL record every live instance of a multi-instance tool,
under its own instance identifier, with its own placement, settings payload and
label. Restore SHALL bring them back as distinct instances with the same names.
Labels SHALL be stored as an optional per-instance field that is omitted when no
instance has a label, so a workspace without labels serialises exactly as it did
before this requirement, and the workspace catalog version is unchanged.

#### Scenario: Two named tuners survive a restart
- **WHEN** two tuners, one renamed "Violin", are open with different settings,
  and the application is quit and started again
- **THEN** both tuners are restored, in the same places, with their own settings,
  and one is still named "Violin"

#### Scenario: Existing workspaces load unchanged
- **WHEN** a workspace or `.ptsettings` file saved before this change is loaded
- **THEN** it restores exactly as it did before, and saving it again without
  changes writes no label field
