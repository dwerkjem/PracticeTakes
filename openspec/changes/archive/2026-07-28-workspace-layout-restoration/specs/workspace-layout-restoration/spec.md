## ADDED Requirements

### Requirement: Built-in workspaces provide useful starting arrangements
The application SHALL provide immutable built-in workspaces named Vocal Warm-up, Pitch Practice, Spectrum Analysis, and Performance Preparation. Each built-in workspace SHALL define its tool presentations, dock topology, tab selection, split ratios, and supported per-tool settings, and Pitch Practice SHALL be the first-launch and recovery fallback.

#### Scenario: First launch opens a useful workspace
- **WHEN** the application starts without a previously saved active workspace
- **THEN** it opens the Pitch Practice built-in workspace with the tuner visible and ready for use

#### Scenario: User applies a built-in workspace
- **WHEN** the user selects one of the four built-in workspaces
- **THEN** the application replaces the active workspace with a working copy of that built-in definition without modifying the immutable built-in

### Requirement: Workspace snapshots retain restorable state
The application SHALL represent a workspace snapshot with stable tool identifiers, each tool's closed, docked, or floating presentation, recursive dock topology, split orientation and ratio, tab order and active tab, floating-window bounds, focused tool, and supported per-tool settings. Applying a snapshot SHALL NOT change global theme, microphone, input-gain, audio-device, fullscreen, or feedback preferences.

#### Scenario: Complex workspace survives restart
- **WHEN** the user exits with a valid workspace containing nested splits, a tab group, adjusted split ratios, and at least one floating tool with resized bounds
- **THEN** the next application start restores those tool presentations, topology, active tabs, ratios, and floating bounds

#### Scenario: Supported tool settings survive restoration
- **WHEN** a workspace is captured with non-default supported settings for a tool and is later restored
- **THEN** the restored tool uses the captured settings

#### Scenario: Applying a workspace preserves global preferences
- **WHEN** the user applies a built-in or named workspace
- **THEN** the current global theme, audio, fullscreen, and feedback preferences remain unchanged

### Requirement: Runtime layout changes update the active snapshot
The application SHALL update the active workspace state when tools open, close, dock, float, move, resize, change tabs, change split ratios, gain focus, or change supported workspace-scoped tool settings. The latest valid active state SHALL be persisted as part of normal settings saving and orderly shutdown.

#### Scenario: Selected tab is retained
- **WHEN** the user selects a different tab in a docked tab group and restarts the application
- **THEN** that tab is selected after restoration

#### Scenario: Divider adjustment is retained
- **WHEN** the user moves a split divider and restarts the application
- **THEN** the restored split uses the resulting ratio subject to current minimum pane sizes

### Requirement: Users can manage named workspaces
The application SHALL let users save the active workspace under a non-empty user-visible name, apply a saved workspace, rename it, overwrite it from the active workspace, and delete it. Built-in workspaces SHALL remain available and SHALL NOT be renamed, overwritten, or deleted.

#### Scenario: Save and apply a named workspace
- **WHEN** the user saves the active workspace with a valid new name and later applies that name
- **THEN** the saved snapshot becomes the active workspace

#### Scenario: Duplicate name requires overwrite confirmation
- **WHEN** the user attempts to save or rename a workspace to a name already used by a named workspace
- **THEN** the application requires explicit overwrite confirmation before replacing the existing named workspace

#### Scenario: Rename a named workspace
- **WHEN** the user gives a saved workspace a valid unused name
- **THEN** the same saved workspace is listed under the new name and no longer under the old name

#### Scenario: Delete a named workspace
- **WHEN** the user confirms deletion of a named workspace
- **THEN** the saved entry is removed while the current live workspace remains unchanged

### Requirement: Workspace restoration recovers from tool and display changes
The application SHALL resolve known renamed tool identifiers through explicit aliases, omit unavailable tools, remove duplicate placements, collapse invalid empty branches and one-item tab groups, select a surviving tab, constrain split ratios, and place floating windows within a connected display's usable bounds.

#### Scenario: Renamed tool is restored through an alias
- **WHEN** a stored workspace references a previous identifier with a registered alias for an available tool
- **THEN** the tool is restored under its current identifier in the original valid placement

#### Scenario: Missing tool does not prevent restoration
- **WHEN** a stored workspace references a tool that is not available and has no registered alias
- **THEN** the application omits that tool, normalizes the remaining layout, and opens the recovered workspace

#### Scenario: Floating window was saved on a disconnected display
- **WHEN** restored floating bounds do not intersect any connected display's usable area
- **THEN** the application moves and, if needed, resizes the window into a connected display's usable area

### Requirement: Invalid workspace data falls back without blocking startup
The application SHALL validate persisted workspace data before applying it. If the active snapshot cannot be recovered into a non-empty layout, the application SHALL use Pitch Practice while preserving independently valid non-workspace settings and valid named workspaces where possible.

#### Scenario: Active workspace document is malformed
- **WHEN** startup encounters malformed or structurally invalid active-workspace data
- **THEN** startup completes with Pitch Practice and valid global settings remain applied

#### Scenario: No known tool survives recovery
- **WHEN** every tool in the stored active workspace is unavailable or invalid
- **THEN** startup completes with Pitch Practice

### Requirement: Users can reset the active workspace
The application SHALL provide a confirmed reset action that replaces the active workspace with Pitch Practice without deleting named workspaces or changing unrelated global settings.

#### Scenario: Reset layout to the default
- **WHEN** the user confirms the layout reset action
- **THEN** Pitch Practice becomes active while named workspaces and global preferences remain unchanged
