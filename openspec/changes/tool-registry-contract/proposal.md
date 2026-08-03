## Why

Five queued roadmap issues each add a new analysis tool (#25 loudness, #26 pitch
velocity, #27 sustain/legato, #28 harmonic spectrum, #29 tempo/onset), and every
one of them is blocked on this change. Adding a tool today means editing a
hardcoded `MainComponent::ToolType` enum and roughly eight files of
`if (tool == ToolType::x)` chains, plus three more parallel `std::unique_ptr`
members and a saved-bounds field on `MainComponent`. That cost is paid five more
times unless the shell stops naming tools individually.

## What Changes

- Introduce a **tool registry**: one declarative entry per tool carrying its
  stable id, user-facing name, factory, preferred floating size, instance
  policy, and per-instance settings codec. Registering a tool is the single
  edit needed to add one.
- Separate **tool identity** (`tuner` — what kind of tool) from **instance
  identity** (`tuner` — which live copy). They stay 1:1 in this change, but they
  become distinct types so that multi-instance support can be added later by
  widening instance-id generation alone, without redesigning the workspace
  model, the persisted format, or the registry.
- Replace `MainComponent`'s per-tool members (`tunerWindow`/`tunerDock`/
  `tunerComponent`/`tunerState`/`savedTunerBounds`, times three) with a single
  keyed collection of live tool instances.
- Define a **shared-service bundle** passed to every tool factory, giving tools
  access to audio input, appearance/theme, and settings without reaching into
  the shell. Tools cannot outlive the bundle by construction.
- Move per-tool settings persistence behind a registry-declared codec, so the
  tuner's bespoke `savedTunerSettings` path and the `dynamic_cast` chain in
  `applyAppearanceToTool` stop being special cases.
- Document the tool lifecycle (register → create → present → move → close →
  destroy) and cover it with tests that do not need a display.

Not in scope: creating more than one live instance of the same tool. The
registry declares each tool's instance policy and the shell enforces it; every
tool ships as single-instance. See `design.md` for the seams this leaves.

## Capabilities

### New Capabilities
- `tool-registry`: how analysis tools are declared, discovered, instantiated,
  given shared services, presented, persisted per instance, and destroyed;
  including the single-instance/multi-instance policy and the identity split
  between a tool and its instances.

### Modified Capabilities
<!-- None. Instance ids remain 1:1 with tool ids in this change, so the
     observable behaviour covered by workspace-layout-restoration and
     workspace-tab-drag is unchanged; both specs keep passing as written. -->

## Impact

**Code**

- `src/application/shell/MainComponent.h` — `ToolType` enum, `allToolTypes`,
  and eleven per-tool members and accessors removed in favour of registry
  lookups and one instance collection.
- `src/application/shell/ui/workspace/shell/MainComponentWorkspacePresentation.cpp`
  — `createToolComponent`, `toolName`, `preferredToolWindowSize`, `stateFor`,
  `componentFor`, `windowFor`, `dockFor`, `detachToolPresentation`, and
  `applyAppearanceToTool` become registry-driven.
- `src/application/shell/ui/workspace/model/WorkspaceToolRegistry.h` — grows
  from an id/alias table into the full contract, or is superseded by a new
  header alongside it.
- `MainComponentWorkspaceMenu.cpp`, `MainComponentWorkspaceSnapshot.cpp`,
  `MainComponentSettings.cpp`, `MainComponentAppearance.cpp`,
  `MainComponentTestControl.cpp`, `MainComponentWorkspaceLayout.cpp`,
  `MainComponentWorkspaceDrag.cpp` — hardcoded tool branches replaced by
  iteration over registered tools.
- `src/features/analysis/{tuner,spectrogram,harmonics}` — each gains a
  registration entry; component code otherwise unchanged.

**Persistence**

- No workspace-catalog format change. `WorkspaceCatalog::currentVersion` stays
  at 1, and existing `.ptsettings` files and saved workspaces load unchanged,
  because instance ids serialise identically to today's tool ids.

**Tests**

- New tests for registry lookup, instance-policy enforcement, lifecycle
  ordering, and service-lifetime safety, under
  `src/tests/application/shell/ui/workspace/model/`.
- Existing workspace model tests (`WorkspaceSnapshotApplyTests`,
  `WorkspaceRecoveryTests`, `WorkspaceLayoutStateTests`,
  `WorkspaceCatalogCodecTests`) must keep passing unchanged — they pin the
  tool-id uniqueness invariant this change deliberately preserves.

**Downstream**

- Unblocks #25, #26, #27, #28, #29. Issue #30 (adapting tuner and spectrogram
  to the contract) becomes a thin follow-up rather than a rewrite.
