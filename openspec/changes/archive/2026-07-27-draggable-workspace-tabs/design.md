## Context

The workspace drag-and-drop system has one drop-zone consumer (`MainComponent`,
implementing `juce::DragAndDropTarget`) and, today, three drag sources that all
funnel through `MainComponent::beginToolDrag(ToolType, juce::Component&)`:

- `ToolDragHandle` — the explicit "Drag" button on a floating `ToolWindow`
- `DockedToolPanel` — mouse-down/drag on a docked panel's header/background
- `ToolWindow` — mouse-down/drag on a floating window's own background

Each owns `mouseDown`/`mouseDrag`/`mouseUp` outright and arms a drag with a
private `dragArmed`/`dragStarted` pair, checking `event.getDistanceFromDragStart()
>= 5` in `mouseDrag` before calling `onDrag`/`beginToolDrag`.

Tab strips are plain `juce::TabbedComponent` instances built in
`MainComponent::buildWorkspaceNode` (`MainComponentWorkspaceLayout.cpp`). Their
tab buttons are stock `juce::TabBarButton`, which is itself a `juce::Button` —
unlike the three existing sources, it already has meaningful default mouse
behavior (click switches the active tab) that must keep working when a press
doesn't turn into a drag.

## Goals / Non-Goals

**Goals:**
- Any visible tab button in any workspace tab strip can initiate a workspace
  tool-drag using the same `beginToolDrag`/`MainComponent` drop-zone pipeline
  as the other three sources.
- Preserve normal click-to-switch-tab behavior when no qualifying drag occurs.
- Drag activates on: movement past the standard 5px threshold, OR any
  movement at all while Ctrl is held (Ctrl lowers the threshold, it does not
  trigger a drag on a perfectly static click).
- The dragged tab visually looks "picked up": a live snapshot of the tab
  button follows the cursor as the drag image, and the source button is
  dimmed/suppressed in the strip for the duration of the drag.

**Non-Goals:**
- No changes to `WorkspaceLayoutState`, drop-zone geometry/resolution
  (`resolveDropTarget`, `dropZoneForPosition`), or the
  `beginToolDrag`/`draggedTool` description-encoding contract.
- No tab reordering-within-a-strip gesture — a tab drag is exactly the same
  "move this tool somewhere in the workspace" operation as the other three
  sources; reordering tabs within one group is out of scope.
- No changes to `TabbedComponent`'s tab content, colours, or non-drag visuals
  beyond the picked-up feedback.

## Decisions

**1. Subclass `juce::TabBarButton`, override `TabbedButtonBar::createTabButton`.**
`juce::TabbedComponent::createTabBarInternal()` is a virtual factory method
specifically meant to be overridden to install a custom `TabbedButtonBar`.
That custom bar overrides `createTabButton(name, index)` to return a
drag-aware `TabBarButton` subclass. This is the standard JUCE extension point
for this exact need, and avoids fighting the framework's own tab-click
handling.

*Alternative considered*: intercept mouse events at the `TabbedButtonBar` or
`TabbedComponent` level via `addMouseListener`. Rejected — the button, not
the bar, owns the click semantics (`Button::mouseUp` decides whether a click
"counts"), and per-button hit-testing/state (which tab, dimmed-while-dragging)
is naturally per-button state, not bar-level state.

**2. Layer drag detection alongside `Button`'s own mouse handling, don't
replace it.**
Override `mouseDown`/`mouseDrag`/`mouseUp` in the subclass, call
`TabBarButton::mouseDown/mouseDrag/mouseUp` (the base) to preserve stock
click/hover/pressed visuals, and only short-circuit the *click action* (tab
switch) when a drag actually started. Concretely: track `dragArmed`/
`dragStarted` exactly like `DockedToolPanel` does; in `mouseUp`, if
`dragStarted` was true, do not forward to base `mouseUp` (which is what
triggers `TabbedButtonBar::setCurrentTabIndex` via the button's click
callback) — otherwise forward normally.

*Alternative considered*: set `setInterceptsMouseClicks` tricks or swallow
all clicks unconditionally post-drag-threshold-support. Rejected as more
fragile than a direct boolean gate on forwarding to the base `mouseUp`.

**3. Ctrl-lowers-threshold, not Ctrl-forces-drag-on-click.**
In `mouseDrag`, arm immediately (`distance >= 5 || event.mods.isCtrlDown()`)
— but this check only runs from inside `mouseDrag`, which JUCE only calls
once the mouse has actually moved (any nonzero delta) while held. A static
click+release with Ctrl held never enters `mouseDrag`'s movement branch, so
it still falls through to a normal tab switch. This matches the proposal's
"movement threshold OR ctrl is held" phrasing without adding a special case
for zero-movement Ctrl-clicks (which was explicitly out of scope per the
exploration).

**4. Picked-up visual: real drag image + dim source, reusing `beginToolDrag`
but with a non-empty image.**
`beginToolDrag` currently always passes `juce::ScaledImage()` (empty) to
`startDragging`. Extend `beginToolDrag` with an optional drag-image parameter
(default remains empty, so the other three sources are unaffected) so the
tab button can pass
`juce::ScaledImage(tabButton.createComponentSnapshot(tabButton.getLocalBounds()))`.
The button additionally sets a `draggingSnapshot` flag consulted from `paint()`
to render itself at reduced alpha (or skip painting the label/background)
while `dragStarted` is true, cleared in `mouseUp`/`itemDragEnd` — mirroring
how `MainComponentWorkspaceDrag.cpp` already gates the drop-zone overlay
purely on "is a drag active", not on which zone is hovered (a documented past
bug/lesson in this codebase).

**5. Tool identity for a tab button comes from its bar-relative tab index,
resolved against `WorkspaceLayoutState::Node::tabs` at build time.**
`buildWorkspaceNode`'s tabs branch already iterates `node->tabs` in the same
order used for `tabs->addTab(...)`, so the custom `TabbedButtonBar`/button can
be constructed with a `std::function<ToolType(int tabIndex)>` (or the
resolved `ToolType` baked in directly per-button at `addTab` time) — no new
lookup mechanism, no coupling to `allToolTypes` beyond what already exists.

## Risks / Trade-offs

- **[Risk]** Overriding `createTabButton` interacts with JUCE's internal tab
  bar layout/animation code (scroll buttons, extra-tabs dropdown) in ways not
  obvious from the header alone. → Mitigation: keep the subclass minimal
  (mouse handling + paint dimming only), let all layout/sizing/scroll logic
  stay in the base `TabBarButton`.
- **[Risk]** Swallowing `mouseUp` after a drag could interfere with JUCE's
  internal "button no longer pressed" state if the base method isn't called
  at all. → Mitigation: still call `TabBarButton::mouseUp` but check whether
  suppressing just the resulting click is possible; if not cleanly
  separable, call base `mouseUp` with a modified event/guard flag the base
  respects, and verify visually (no stuck "pressed" look) before dragStarted
  paths ship. Buildable/verifiable in dev before this is considered done.
- **[Trade-off]** Dimming via `paint()` override rather than swapping in a
  fully separate floating "ghost" component keeps the change small and
  consistent with `beginToolDrag`'s existing single-call-site pattern, at the
  cost of a slightly less polished dim transition than a true ghost overlay.

## Migration Plan

No data/schema migration. This is additive UI behavior gated entirely behind
new mouse-event code paths in a new tab-bar-button subclass; existing
non-drag tab click/switch behavior is preserved via the base-class fallback,
so no feature flag or staged rollout is needed.

## Open Questions

None outstanding — scope, activation, and visual feedback were confirmed
during exploration.
