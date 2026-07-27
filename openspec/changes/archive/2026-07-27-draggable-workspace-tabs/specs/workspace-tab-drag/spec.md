## ADDED Requirements

### Requirement: Tab labels initiate a workspace tool drag
Every tab button rendered in a workspace tab strip SHALL be able to initiate
the same workspace tool drag used by the drag handle, docked panel, and
floating window sources, feeding the existing drop-zone resolution and
`itemDropped` handling without any per-tool or per-tab-strip exclusion.

#### Scenario: Dragging a tab past the movement threshold starts a tool drag
- **WHEN** the user presses and holds a tab button, then moves the pointer at
  least the standard drag-start distance (5px) before releasing
- **THEN** the workspace begins a tool drag for that tab's tool, identical to
  a drag started from that tool's drag handle, docked panel, or floating
  window

#### Scenario: Every visible tab strip supports tab dragging
- **WHEN** the workspace contains multiple tab groups (nested splits each
  containing their own tabs)
- **THEN** tab buttons in every one of those groups support drag-initiation,
  not only the first or a designated primary strip

### Requirement: Ctrl held lowers the drag activation threshold
Holding Ctrl while the pointer moves during a tab button press SHALL arm a
tool drag on any nonzero movement, without requiring the standard 5px
threshold to be met.

#### Scenario: Ctrl held with small movement starts a drag
- **WHEN** the user presses a tab button, holds Ctrl, and moves the pointer
  by less than the standard drag-start distance
- **THEN** the workspace begins a tool drag for that tab's tool

#### Scenario: Ctrl held with zero movement performs a normal tab switch
- **WHEN** the user presses and releases a tab button with Ctrl held but
  never moves the pointer
- **THEN** no tool drag begins and the tab strip switches its active tab as
  it would without Ctrl held

### Requirement: Non-drag tab clicks keep switching tabs
A press-and-release on a tab button that never crosses the drag activation
condition SHALL continue to switch the active tab exactly as stock tab-strip
behavior does today.

#### Scenario: Plain click switches tabs
- **WHEN** the user presses and releases a tab button without moving the
  pointer past the drag activation condition
- **THEN** that tab becomes the active tab in its strip and no tool drag is
  started

### Requirement: Dragged tab renders as picked up
While a tab-initiated tool drag is active, the system SHALL present the
dragged tab as visually lifted from the strip: the drag image SHALL be a
snapshot of the tab button itself, and the source tab button SHALL be
visually suppressed (e.g. dimmed) in the strip for the duration of the drag.

#### Scenario: Drag image matches the tab being dragged
- **WHEN** a tab-initiated tool drag begins
- **THEN** the image following the pointer is a rendered snapshot of that tab
  button, not an empty or generic drag image

#### Scenario: Source tab is suppressed during the drag
- **WHEN** a tab-initiated tool drag is in progress
- **THEN** the original tab button in the strip renders in a visually
  suppressed (dimmed) state until the drag ends

#### Scenario: Tab strip returns to normal after the drag ends
- **WHEN** a tab-initiated tool drag ends (dropped or cancelled)
- **THEN** the tab button returns to its normal, non-suppressed appearance
