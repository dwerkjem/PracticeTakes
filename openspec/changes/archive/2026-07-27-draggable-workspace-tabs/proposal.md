## Why

Every other way to relocate a docked/floating tool (drag handle, panel header,
floating window background) already starts a workspace tool-drag, but a tab
label inside a tab strip is inert — dragging it does nothing. Users expect a
tab, like a window title, to be draggable to retile or regroup it, so tab
strips are currently the one inconsistent corner of the drag-and-drop docking
system.

## What Changes

- Tab buttons in every visible workspace tab strip become a fourth drag
  source feeding the existing `beginToolDrag` → `MainComponent` drop-zone
  pipeline, alongside `ToolDragHandle`, `DockedToolPanel`, and `ToolWindow`.
- A tab drag arms on mouse-down and activates once the pointer moves past the
  existing 5px threshold, or immediately once Ctrl is held during movement.
  A press+release with no qualifying movement still performs the normal
  JUCE tab-switch click.
- While a tab drag is active, the dragged tab renders "picked up": the drag
  image is a live snapshot of the tab button (not the default empty image),
  and the source tab button is visually suppressed/dimmed in the strip until
  the drag ends.
- Applies uniformly to all tab strips built by `buildWorkspaceNode` — no
  per-tool or per-group opt-out.

## Capabilities

### New Capabilities
- `workspace-tab-drag`: Dragging a tab label in any workspace tab strip
  behaves like dragging that tool's window/panel — arming, activation
  threshold (movement or Ctrl), picked-up visual feedback, and integration
  with the existing drop-zone resolution and tab-switch click behavior.

### Modified Capabilities
(none — `openspec/specs/` has no existing capability specs yet)

## Impact

- `src/application/shell/ui/workspace/MainComponentWorkspaceLayout.cpp` —
  `buildWorkspaceNode` must install a custom tab bar/button factory on each
  `juce::TabbedComponent` it creates.
- New component(s) under `src/application/shell/ui/workspace/` for the
  drag-aware `juce::TabBarButton` (and a `juce::TabbedButtonBar` override to
  install it), following the existing `ToolDragHandle`/`DockedToolPanel`
  mouse-handling pattern.
- No changes to `WorkspaceLayoutState`, `MainComponentWorkspaceDrag.cpp`'s
  drop-zone resolution, or the `beginToolDrag`/`draggedTool` contract — the
  new source reuses them as-is.
