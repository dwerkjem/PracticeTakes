## MODIFIED Requirements

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

## ADDED Requirements

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
The shell SHALL NOT create an instance of a tool that takes an audio consumer
unless the shared audio input service has a free consumer slot. When none is
free, "Open another" SHALL be disabled and SHALL say that there is no audio
capacity left. Restoring a workspace SHALL open audio-consuming instances in
the order the shell lists them while slots remain, and leave the rest closed and
out of the tiling tree, without failing
the restore. The number of consumer slots, their preallocation and the audio
callback SHALL be unchanged.

#### Scenario: The menu says why another tuner can't open
- **WHEN** every audio consumer slot is in use and the user opens the Tuner
  submenu
- **THEN** "Open another" is disabled and its text says there is no audio
  capacity left

#### Scenario: An oversized workspace restores what fits
- **WHEN** a workspace holding more microphone instances than there are free
  slots is applied
- **THEN** instances open until the slots are used, the
  rest stay closed, and the workspace is otherwise restored

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
