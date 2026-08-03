## 1. Pure tool identity and catalog

- [x] 1.1 Add `src/application/tools/ToolInstanceId.h`: a string-backed instance
      id with `toolId()` (text before `#`), `ordinal()`, and a factory that
      builds the nth instance id for a tool id. No JUCE include.
- [x] 1.2 Add `src/application/tools/ToolCatalog.h`: `ToolDefinition { id,
      displayName, aliases, instancePolicy, settingsVersion, preferredSize }`
      plus `find`/`resolve`, carrying over `WorkspaceToolRegistry`'s alias
      behaviour. Reject a tool id containing `#` at construction. No JUCE
      include.
- [x] 1.3 Add `src/tests/application/tools/ToolInstanceIdTests.cpp` and
      `ToolCatalogTests.cpp`: id round-trip, `#`-in-tool-id rejection, alias
      resolution, unknown-id miss. Register both in `CMakeLists.txt`.
- [x] 1.4 Delete `WorkspaceToolRegistry.h` and point `WorkspaceNormalizer.h`,
      `WorkspaceSnapshotApply.h`, and their tests at `ToolCatalog`. Behaviour
      unchanged; the existing workspace model tests must pass untouched.

## 2. Tool contract and registry

- [x] 2.1 Add `src/application/tools/ToolComponent.h`: `ToolComponent :
      public juce::Component` with `setTheme(Theme)` and the settings
      capture/apply hooks.
- [x] 2.2 Add `src/application/tools/ToolServices.h`: the shared-service bundle
      passed to factories. Resolve the design's open question on whether theme
      travels in the bundle or stays a `setTheme` push.
- [x] 2.3 Add `src/application/tools/ToolRegistry.h`: wraps a `ToolCatalog` and
      adds per-tool factory and settings codec. Resolve the design's open
      question on where `preferredSize` lives.
- [x] 2.4 Add `src/application/tools/BuiltInTools.cpp`: the one place tuner,
      spectrogram, and harmonic analyzer are registered, all single-instance,
      with the ids and aliases the current `WorkspaceToolRegistry` declares.

## 3. Make the three existing tools conform

- [x] 3.1 Make `TunerComponent`, `SpectrogramComponent`, and
      `HarmonicAnalyzerComponent` derive from `ToolComponent`.
- [x] 3.2 Move the tuner's settings capture/apply behind its registry-declared
      codec, replacing `MainComponent::savedTunerSettings` and the
      `dynamic_cast<TunerComponent*>` in `closeTool`.
- [x] 3.3 Confirm the spectrogram and harmonic analyzer declare no settings
      codec and restore to defaults, matching current behaviour.

## 4. Replace MainComponent's per-tool storage

- [x] 4.1 Add the `LiveTool` struct and `std::vector<LiveTool> liveTools` to
      `MainComponent.h`, declared after `audioInputService` and
      `appLookAndFeel`, with a comment stating that the declaration order is
      what guarantees instances die before their services.
- [x] 4.2 Delete `ToolType`, `allToolTypes`, the eleven per-tool members, and
      the `stateFor`/`componentFor`/`windowFor`/`dockFor` accessors; replace
      with instance-id lookup over `liveTools`.
- [x] 4.3 Rewrite `MainComponentWorkspacePresentation.cpp` against the registry:
      `createToolComponent` calls the factory, `toolName` and
      `preferredToolWindowSize` read the definition, `applyAppearanceToTool`
      calls `ToolComponent::setTheme` with no `dynamic_cast`.
- [x] 4.4 Add the single-instance policy check on the open path: opening a tool
      that already has a live instance focuses it instead of creating a second.
- [x] 4.5 Assign `WorkspaceLayoutState::Tool` handles per live instance from a
      counter; leave `WorkspaceLayoutState` itself unchanged.

## 5. Generalise the remaining shell call sites

- [x] 5.1 `MainComponentWorkspaceMenu.cpp`: build the Tools menu by iterating
      registered tools instead of the three hardcoded blocks at lines 140-188.
- [x] 5.2 `MainComponentWorkspaceSnapshot.cpp`: build the `ToolBinding` list
      from live instances instead of the hardcoded arrays at lines 112 and 138;
      key capture and restore by instance id.
- [x] 5.3 `MainComponentAppearance.cpp`: iterate live instances instead of the
      three `applyAppearanceToTool` calls at lines 105-113.
- [x] 5.4 `MainComponentSettings.cpp`: replace the `currentTool` branches at
      lines 395-403, 525-529, and 590-598 with registry lookups.
- [x] 5.5 `MainComponentTestControl.cpp`: resolve approved state tool names
      through the catalog; retire `testcontrol::knownToolNames()` and its
      pinning test now that tool identity is JUCE-free.
- [x] 5.6 `MainComponentWorkspaceLayout.cpp` and `MainComponentWorkspaceDrag.cpp`:
      swap `dockFor(tool)` for instance lookup.

## 6. Move dedup to instance ids

- [x] 6.1 Change the uniqueness checks in `WorkspaceNormalizer` (lines 59, 147,
      195) and `WorkspaceSnapshotApply` (lines 74, 195, 212) to key on instance
      id rather than resolved tool id.
- [x] 6.2 Confirm `WorkspaceSnapshotApplyTests`, `WorkspaceRecoveryTests`,
      `WorkspaceLayoutStateTests`, and `WorkspaceCatalogCodecTests` pass with no
      edits — they pin the invariant this change preserves.
- [x] 6.3 Add a restoration test: a snapshot naming a single-instance tool twice
      restores to exactly one instance and still succeeds.

## 7. Lifecycle and policy tests

- [x] 7.1 Add `src/tests/application/tools/ToolLifecycleTests.cpp` asserting the
      create → restore-settings → attach → detach → capture-settings → destroy
      order without constructing a JUCE window. Register in `CMakeLists.txt`.
- [x] 7.2 Add a policy test registering a fake multi-instance tool and asserting
      two distinct instance ids, so the `multi` branch is exercised even though
      no shipped tool uses it.
- [x] 7.3 Add a test that a settings payload whose version differs from the
      declared `settingsVersion` is discarded and the instance starts at
      defaults.
- [x] 7.4 Add the "new tool needs only a registry entry" regression test:
      register a throwaway tool in a test build and assert it reaches the menu
      model, both presentations, and workspace capture.

## 8. Documentation and verification

- [x] 8.1 Document the lifecycle and the shared-service contract in
      `docs/development/architecture/ARCHITECTURE.md`, and note the tool-registry
      layer in the source-layering section of
      `docs/development/agents/AGENT_GUIDE.md`.
- [x] 8.2 Write `docs/development/architecture/adding-a-tool.md`: the single
      registry entry, what each field means, and the instance-policy field with
      a note that `multi` is declared but unexercised.
- [x] 8.3 Run `ctest --test-dir build --output-on-failure`, then
      `python tools/scripts/quality/run_clang_tidy.py` over the changed sources
      and `pre-commit run --all-files`.
- [x] 8.4 Verified workspace capture and restore end to end. `run-ui-golden.zsh`
      could not complete on this machine — it fails identically on `main`
      (`xdotool` is absent, and the script looks for the settings file at a path
      this Linux profile layout does not use), so it was not usable as a
      regression signal. Verified directly instead: launched the app under a
      clean `HOME` with `--ui-validation-scenario prepare-restored`, confirmed
      the persisted catalog is structurally identical to the pre-registry
      format (`"toolId": "tuner"`, `"toolSettings": {"tuner": ...}`, catalog
      `"version": 1`, legacy `tuner.*` and `workspace.lastFloating.*` keys
      intact), then relaunched against that profile and confirmed the split,
      tab group, active tab, focus, and tuner settings restore and re-save
      byte-identically. The `juce_Component.cpp:2752` focus assertion in the
      log predates this change and reproduces on `main`.
      **Still owed: a human GUI pass** covering docked/floating/tiled/tabbed
      moves and the Tools menu, since MainComponent remains outside
      `PracticeTakesTests`.
- [x] 8.5 Open the PR against `main` referencing issue #24, and note in the
      description that multi-instance is declared-only with the seams recorded
      in `design.md`.
