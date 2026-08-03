## Context

`MainComponent` names every tool individually. A private
`enum class ToolType { tuner, spectrogram, harmonics }` drives eleven members
(`tunerWindow`/`tunerDock`/`tunerComponent`/`tunerState`/`savedTunerBounds`,
three times over) and a chain of `if (tool == ToolType::x)` accessors —
`createToolComponent`, `toolName`, `preferredToolWindowSize`, `stateFor`,
`componentFor`, `windowFor`, `dockFor` — plus `dynamic_cast` ladders in
`applyAppearanceToTool` and `closeTool`. Adding a fourth tool touches eight
files. Five queued issues each add one.

Three things already point the right way and this design leans on all of them:

1. `WorkspaceLayoutState::Tool` is deliberately an opaque `enum class Tool : int`
   with no fixed roster — its own comment says "callers map their own tool
   enumeration onto these ids". It needs no change at all.
2. The persisted `WorkspaceSnapshot` is already keyed by `std::string` tool id,
   with a `std::map<std::string, ToolSettingsPayload> toolSettings`.
3. `WorkspaceSnapshotApply::ToolBinding { std::string id; WorkspaceLayoutState::Tool tool; }`
   is already the indirection between string ids and layout handles. Today the
   binding list is hardcoded at `MainComponentWorkspaceSnapshot.cpp:112`; it
   just needs to be built from live instances instead.

Working against that, tool-id uniqueness is a deliberately enforced invariant in
four places — `WorkspaceNormalizer` (drops duplicates), `WorkspaceSnapshotApply`
(rejects the whole plan), `WorkspaceLayoutState::replaceRoot` (rejects duplicate
placement), and `WorkspaceSnapshot::presentationOf` (one presentation per id) —
pinned by tests at `WorkspaceSnapshotApplyTests.cpp:52,62`,
`WorkspaceLayoutStateTests.cpp:433`, and `WorkspaceRecoveryTests.cpp:8`.

## Goals / Non-Goals

**Goals:**

- One registry entry is the only edit needed to add a tool.
- Tool identity and instance identity are separate types from the start.
- Shared services reach tools through one bundle; instances cannot outlive it.
- Per-instance settings serialise through a registry-declared codec.
- The registry, instance policy, and lifecycle ordering are testable headless.
- No workspace-catalog format change; existing saved workspaces keep loading.

**Non-Goals:**

- Creating a second live instance of any tool. Every tool ships single-instance.
- Any UI for duplicating a tool.
- Changing what the tuner, spectrogram, or harmonic analyzer display. Issue #30
  covers adapting their internals; this change only re-homes how they are
  constructed and themed.
- Reworking `WorkspaceLayoutState`, the catalog codec, or the tiling engine.

## Decisions

### Instance ids are strings that extend tool ids, not replace them

A `ToolInstanceId` is a string. For the first instance of a tool it is exactly
the tool id — `"tuner"`. A hypothetical later instance would be `"tuner#2"`:
the tool id, a `#`, and an ordinal. `ToolInstanceId::toolId()` returns the part
before the `#`.

This is the central seam. Because every tool ships single-instance, every
instance id emitted by this change is byte-identical to the tool id written
today, so `WorkspaceCatalog::currentVersion` stays at 1, saved workspaces and
`.ptsettings` files load unchanged, and `WorkspaceBuiltIns` needs no edit.
Adding multi-instance later changes instance-id *generation* and relaxes one
policy check — it does not change the format, the codec, or the registry.

*Alternative rejected:* a separate integer instance handle serialised alongside
the tool id. Cleaner in the abstract, but it forces a v1→v2 catalog migration
now to buy nothing now.

### Dedup keys move to instance ids immediately, even though nothing duplicates yet

The four uniqueness checks change from "unique tool id" to "unique instance id"
in this change. Today the two are the same string, so behaviour is identical and
`WorkspaceSnapshotApplyTests`, `WorkspaceRecoveryTests`, and
`WorkspaceLayoutStateTests` keep passing untouched — a corrupt snapshot naming
`"tuner"` twice still collapses to one.

Doing it now rather than later is what makes multi-instance a small change: the
dedup sites are then already correct, and the only remaining gate is the
instance-policy check below. Deferring it would mean revisiting four pure-logic
classes and their tests under a later change.

### Single-instance enforcement lives in one policy check, not in the dedup

`ToolDefinition::instancePolicy` is `single` or `multi`. The shell consults it
in exactly one place: when asked to create an instance of a tool that already
has one. Under `single` it focuses the existing instance and returns; under
`multi` it mints the next instance id.

Restoring a snapshot that somehow names `"tuner"` and `"tuner#2"` therefore
survives dedup (different instance ids) and is then reduced by the policy check
to one instance, satisfying the spec scenario without the dedup sites needing to
know anything about policy. Multi-instance later means flipping one tool's
declared policy — the shell code is already written for it.

### Split the contract into a pure catalog and a JUCE-facing registry

`src/application/tools/ToolCatalog.h` — no JUCE include — holds
`ToolDefinition { id, displayName, aliases, instancePolicy, settingsVersion,
preferredSize }` and lookup/alias resolution. `src/application/tools/ToolRegistry.h`
wraps a catalog and adds the JUCE-dependent parts: the factory and the settings
codec.

This follows the guide's testability pattern, and it lets instance-policy and
lifecycle-ordering tests run without a display. `WorkspaceToolRegistry` is
today's pure id/alias table and is superseded by `ToolCatalog`: it grows the new
fields and moves, and `WorkspaceNormalizer`/`WorkspaceSnapshotApply` update their
include. Their logic is unchanged.

*Bonus:* `testcontrol::knownToolNames()` exists only because "ToolType itself
cannot be named here because it lives behind JuceHeader"
(`ApprovedWindowStates.h:136`). A JUCE-free `ToolCatalog` removes that
constraint, so the hand-maintained list and its pinning test can resolve against
the catalog instead.

### Tools implement a `ToolComponent` interface instead of being `dynamic_cast`

`ToolComponent : public juce::Component` declares `setTheme(Theme)` and the
settings hooks. The three existing tools already have `setTheme`, so inheriting
is nearly free, and it deletes the `dynamic_cast` ladders in
`applyAppearanceToTool` and `closeTool`.

*Alternative rejected:* type-erased `std::function` adapters stored in the
registry entry. Avoids touching the tool classes, but pushes per-tool glue back
into the registration site — which is the thing this change is trying to remove.

### Live instances become one keyed collection, with destruction order as the lifetime guarantee

The eleven per-tool members collapse to:

```cpp
struct LiveTool {
    ToolInstanceId id;
    std::unique_ptr<ToolComponent> component;
    std::unique_ptr<ToolWindow> window;
    std::unique_ptr<DockedToolPanel> dock;
    WorkspaceToolState state;
    juce::Rectangle<int> savedFloatingBounds;
};
std::vector<LiveTool> liveTools;
```

`liveTools` is declared *after* `audioInputService` and the look-and-feel in
`MainComponent`, so reverse-declaration-order destruction destroys every
instance before the services it borrowed. That is how "tools cannot outlive
required shared services" is satisfied — by construction, not by a runtime
check. A comment at the declaration will say so, because the ordering is load-
bearing and otherwise invisible.

### `WorkspaceLayoutState` is untouched

Layout handles stay opaque ints, assigned per live instance from a counter and
mapped through the existing `ToolBinding` indirection. The 532-line
`WorkspaceLayoutStateTests.cpp` needs no change.

## Risks / Trade-offs

- **`MainComponent` is largely outside `PracticeTakesTests`** (`QA_STRATEGY.md`
  area 9), and this change rewrites its core. → Land it in small commits; keep
  the pure-logic parts (`ToolCatalog`, instance-id parsing, policy, lifecycle
  ordering) under headless tests; verify the shell through the existing
  test-control surface, `run-ui-golden.zsh`, and a manual GUI pass before merge.

- **Declaring multi-instance without exercising it risks an untested seam.** The
  policy check's `multi` branch would have no production caller. → Cover it with
  a headless test that registers a fake multi-instance tool and asserts two
  distinct instance ids, so the branch is exercised even though no shipped tool
  uses it.

- **Instance ids embed structure in a string** (`tool#ordinal`), so a tool id
  containing `#` would corrupt parsing. → Reject `#` in tool ids at registration
  with a static assertion or a startup check, and test it.

- **Behaviour drift during the refactor.** Nine call sites move from explicit
  branches to generic iteration; a missed case could silently drop a tool from
  the menu or from appearance updates. → The spec scenario "a new tool needs
  only a registry entry" is the regression test: register a throwaway tool in a
  test build and assert it reaches the menu, both presentations, and workspace
  capture.

- **Wider default `.ptsettings` surface.** More registered tools means more
  potential `toolSettings` entries; `WorkspaceNormalizer::maximumToolSettings`
  is 64. → Ample for the eight tools the roadmap plans; no change needed, noted
  so it is not a surprise later.

## Migration Plan

No data migration. Catalog version stays 1; instance ids serialise as today's
tool ids; built-in workspaces and saved `.ptsettings` files load unchanged.
Rollback is a branch revert — nothing written by this change is unreadable by
the current code.

## Resolved Questions

- **Does `ToolServices` carry an appearance service or just the current
  `Theme`?** Resolved in task 2: the bundle carries `initialTheme` so a tool is
  built already correct, and later changes arrive through
  `ToolComponent::setTheme`, because a theme change has to trigger a repaint and
  not merely update state. One source of truth per concern — construction reads
  the bundle, changes come through the virtual.
- **Where does `preferredSize` live?** Resolved in task 1: it stays in the pure
  `ToolCatalog` as `ToolPreferredSize { int width, height }`, which keeps window
  sizing testable without a display.
- **Does the settings codec belong on the registry or the component?** Not
  anticipated, but it came up building task 2. It went on `ToolComponent` as
  `captureSettings`/`applySettings`, with the *version* declared in the catalog.
  A tool serialising its own settings is cohesive and needs no `dynamic_cast`;
  putting the codec in the registry entry would have pushed per-tool glue back
  into the registration site, which is the thing this change removes.
  `ToolRegistry` therefore holds only factories.
