## ADDED Requirements

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
second one. For a multi-instance tool the shell SHALL create an additional
instance with its own instance identifier. Every tool shipped by this change
SHALL be single-instance.

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
