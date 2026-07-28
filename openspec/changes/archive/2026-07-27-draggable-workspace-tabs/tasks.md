## 1. Drag-aware tab button

- [x] 1.1 Create `WorkspaceTabBarButton` (new header,
      `src/application/shell/ui/workspace/WorkspaceTabBarButton.h`) subclassing
      `juce::TabBarButton`. Implementation note: constructed with a
      `std::function<void(juce::Component&)>` drag-begin callback only (no
      `ToolType` parameter) -- the tool identity is bound via closure at the
      call site in `buildWorkspaceNode`, the same idiom `DockedToolPanel`/
      `ToolWindow` already use, which avoids needing `ToolType` (a private
      `MainComponent` nested type) inside this generic button class.
- [x] 1.2 Implement `mouseDown`/`mouseDrag`/`mouseUp` on
      `WorkspaceTabBarButton`: arm on `mouseDown`; in `mouseDrag`, start the
      drag when `event.getDistanceFromDragStart() >= 5 ||
      event.mods.isCtrlDown()` and not already started; call the base
      `TabBarButton` methods so stock hover/pressed visuals and click-to-switch
      behavior are preserved when no drag occurs.
- [x] 1.3 Suppress the normal tab-switch click when a drag started: track
      `dragStarted` and skip forwarding to `TabBarButton::mouseUp` (or
      otherwise prevent the click callback) only in that case; verify a plain
      click with no qualifying movement still switches tabs.
- [x] 1.4 Add "picked up" rendering: an `isBeingDragged` flag set when the
      drag starts and cleared on `mouseUp`/drag end, consulted from
      `paintButton()` to render the button in a dimmed transparency layer
      while true.

## 2. Custom tab bar wiring

- [x] 2.1 (Revised from design) JUCE's `createTabButton(name, tabIndex)` is
      a protected virtual directly on `juce::TabbedComponent` (its internal
      `TabbedButtonBar` delegates to it) -- no separate `TabbedButtonBar`
      subclass is required. `WorkspaceTabbedComponent::createTabButton`
      overrides it to return a `WorkspaceTabBarButton`, looking up the
      per-tab drag handler recorded by `addToolTab` at the matching index.
- [x] 2.2 Created `WorkspaceTabbedComponent` (subclasses
      `juce::TabbedComponent` directly, per 2.1) with `addToolTab(name,
      backgroundColour, content, dragHandler)` replacing the inherited
      `addTab` at call sites that need drag support.
- [x] 2.3 Updated `MainComponent::buildWorkspaceNode`'s tabs branch
      (`MainComponentWorkspaceLayout.cpp`) to construct
      `WorkspaceTabbedComponent` instead of plain `juce::TabbedComponent`,
      calling `addToolTab` with a per-tool drag-begin closure built the same
      way `MainComponentWorkspacePresentation.cpp` builds its `dragHandler`.

## 3. Drag pipeline integration

- [x] 3.1 Extended `MainComponent::beginToolDrag` with an optional drag-image
      parameter (default: empty `juce::ScaledImage()`), so existing
      call sites (`ToolDragHandle`, `DockedToolPanel`, `ToolWindow`) are
      unaffected.
- [x] 3.2 Wired the tab drag closure in `buildWorkspaceNode` to call
      `beginToolDrag` with
      `source.createComponentSnapshot(source.getLocalBounds())` as the drag
      image.
- [x] 3.3 Confirmed the drag flows through the existing
      `isInterestedInDragSource`/`itemDragEnter`/`itemDragMove`/`itemDropped`
      pipeline in `MainComponentWorkspaceDrag.cpp` unchanged — no new branches
      needed there since `draggedTool` only inspects the drag description.

## 4. Verification

- [x] 4.1 Build (`PracticeTakes` target) succeeds; manual GUI verification
      confirmed dragging a tab past the threshold retiles/regroups its tool
      via the same drop-zone overlay as the other three drag sources.
- [x] 4.2 Manually verified Ctrl+small-movement starts a drag, and
      Ctrl+zero-movement click still switches tabs.
- [x] 4.3 Manually verified the dragged tab shows a snapshot drag image and
      the source tab dims until drop/cancel, then returns to normal.
- [x] 4.4 Manually verified tab strips at multiple nesting depths (not just a
      single top-level tab group) all support tab dragging.
- [x] 4.5 Run the existing test suite
      (`./build/PracticeTakesTests`) to confirm no regressions in
      `WorkspaceLayoutStateTests`/`WorkspaceToolStateTests`. Result: all 398
      assertions in 75 test cases passed.

