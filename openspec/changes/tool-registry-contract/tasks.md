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

- [ ] 4.1 Add the `LiveTool` struct and `std::vector<LiveTool> liveTools` to
      `MainComponent.h`, declared after `audioInputService` and
      `appLookAndFeel`, with a comment stating that the declaration order is
      what guarantees instances die before their services.
- [ ] 4.2 Delete `ToolType`, `allToolTypes`, the eleven per-tool members, and
      the `stateFor`/`componentFor`/`windowFor`/`dockFor` accessors; replace
      with instance-id lookup over `liveTools`.
- [ ] 4.3 Rewrite `MainComponentWorkspacePresentation.cpp` against the registry:
      `createToolComponent` calls the factory, `toolName` and
      `preferredToolWindowSize` read the definition, `applyAppearanceToTool`
      calls `ToolComponent::setTheme` with no `dynamic_cast`.
- [ ] 4.4 Add the single-instance policy check on the open path: opening a tool
      that already has a live instance focuses it instead of creating a second.
- [ ] 4.5 Assign `WorkspaceLayoutState::Tool` handles per live instance from a
      counter; leave `WorkspaceLayoutState` itself unchanged.

## 5. Generalise the remaining shell call sites

- [ ] 5.1 `MainComponentWorkspaceMenu.cpp`: build the Tools menu by iterating
      registered tools instead of the three hardcoded blocks at lines 140-188.
- [ ] 5.2 `MainComponentWorkspaceSnapshot.cpp`: build the `ToolBinding` list
      from live instances instead of the hardcoded arrays at lines 112 and 138;
      key capture and restore by instance id.
- [ ] 5.3 `MainComponentAppearance.cpp`: iterate live instances instead of the
      three `applyAppearanceToTool` calls at lines 105-113.
- [ ] 5.4 `MainComponentSettings.cpp`: replace the `currentTool` branches at
      lines 395-403, 525-529, and 590-598 with registry lookups.
- [ ] 5.5 `MainComponentTestControl.cpp`: resolve approved state tool names
      through the catalog; retire `testcontrol::knownToolNames()` and its
      pinning test now that tool identity is JUCE-free.
- [ ] 5.6 `MainComponentWorkspaceLayout.cpp` and `MainComponentWorkspaceDrag.cpp`:
      swap `dockFor(tool)` for instance lookup.

## 6. Move dedup to instance ids

- [ ] 6.1 Change the uniqueness checks in `WorkspaceNormalizer` (lines 59, 147,
      195) and `WorkspaceSnapshotApply` (lines 74, 195, 212) to key on instance
      id rather than resolved tool id.
- [ ] 6.2 Confirm `WorkspaceSnapshotApplyTests`, `WorkspaceRecoveryTests`,
      `WorkspaceLayoutStateTests`, and `WorkspaceCatalogCodecTests` pass with no
      edits — they pin the invariant this change preserves.
- [ ] 6.3 Add a restoration test: a snapshot naming a single-instance tool twice
      restores to exactly one instance and still succeeds.

## 7. Lifecycle and policy tests

- [ ] 7.1 Add `src/tests/application/tools/ToolLifecycleTests.cpp` asserting the
      create → restore-settings → attach → detach → capture-settings → destroy
      order without constructing a JUCE window. Register in `CMakeLists.txt`.
- [ ] 7.2 Add a policy test registering a fake multi-instance tool and asserting
      two distinct instance ids, so the `multi` branch is exercised even though
      no shipped tool uses it.
- [ ] 7.3 Add a test that a settings payload whose version differs from the
      declared `settingsVersion` is discarded and the instance starts at
      defaults.
- [ ] 7.4 Add the "new tool needs only a registry entry" regression test:
      register a throwaway tool in a test build and assert it reaches the menu
      model, both presentations, and workspace capture.

## 8. Documentation and verification

- [ ] 8.1 Document the lifecycle and the shared-service contract in
      `docs/development/architecture/ARCHITECTURE.md`, and note the tool-registry
      layer in the source-layering section of
      `docs/development/agents/AGENT_GUIDE.md`.
- [ ] 8.2 Write `docs/development/architecture/adding-a-tool.md`: the single
      registry entry, what each field means, and the instance-policy field with
      a note that `multi` is declared but unexercised.
- [ ] 8.3 Run `ctest --test-dir build --output-on-failure`, then
      `python tools/scripts/quality/run_clang_tidy.py` over the changed sources
      and `pre-commit run --all-files`.
- [ ] 8.4 Run `./tools/scripts/quality/ui-validation/run-ui-golden.zsh` and a
      manual GUI pass covering tuner and spectrogram docked, floating, tiled,
      tabbed, and restored after restart.
- [ ] 8.5 Open the PR against `main` referencing issue #24, and note in the
      description that multi-instance is declared-only with the seams recorded
      in `design.md`.
