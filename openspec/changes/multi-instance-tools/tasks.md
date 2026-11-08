## 1. Pure rules, tested headless

- [ ] 1.1 Add `src/application/tools/ToolInstanceLabel.h`:
      - `defaultName(displayName, ordinal)`: the display name for ordinal 1,
        and the display name, a space and the ordinal after that;
      - `sanitise(label)`: trim, strip control characters, cap at 40 characters,
        and return empty to mean "use the default".

      No JUCE include.
- [ ] 1.2 Add `ToolInstanceAllocator::instanceToFocus(catalog, toolId, isLive)`:
      the first live instance of a tool, or ordinal 1 when none is live. "Open or
      focus" uses it; `nextInstanceId` stays what "Open another" uses.
- [ ] 1.3 Add `ToolInstanceAllocator::mayOpenAnother(definition, freeConsumerSlots)`:
      false for a single-instance tool, and false for an audio-consuming tool
      when no slot is free.
- [ ] 1.4 Add `bool usesAudioInput` to `ToolDefinition`, defaulting to false,
      and set it for the tuner, spectrogram and harmonic analyzer.
- [ ] 1.5 Tests in `src/tests/application/tools/`: a new
      `ToolInstanceLabelTests.cpp`, and new cases beside the existing allocator
      cases in `ToolLifecycleTests.cpp` for `instanceToFocus`, `mayOpenAnother`,
      and ordinal reuse with the real built-in catalog. Add the new test file to
      `PRACTICE_TAKES_TEST_SOURCES`.

## 2. Labels in the workspace document

- [ ] 2.1 Add `std::map<std::string, std::string> toolLabels` to
      `WorkspaceSnapshot`. Encode it in `WorkspaceCatalogCodec.cpp` only when
      non-empty, decode it when present, sanitise each label, and cap the count
      at `maximumToolSettings`.
- [ ] 2.2 `WorkspaceNormalizer` drops labels for instances it drops, such as a
      single-instance duplicate or an unknown tool.
- [ ] 2.3 Codec tests: a round trip with labels; a document without
      `toolLabels` decodes and re-encodes byte-identical; an oversized or
      control-character label is sanitised. The existing workspace and
      settings-transfer fixtures must pass unchanged.

## 3. Audio consumer capacity

- [ ] 3.1 Add `AudioInputService::freeConsumerSlots()`, message-thread only,
      counted under `consumerLock`. No change to the callback, the slot array
      or `maximumConsumers`. Keep the callback's `noexcept
      PRACTICE_TAKES_NONBLOCKING` annotation untouched.
- [ ] 3.2 Test it in `AudioInputServiceTests.cpp`: eight free slots minus the
      listeners added, back up on removal. The RealtimeSanitizer job must stay
      green.

## 4. Shell

- [ ] 4.1 Flip the tuner, spectrogram and harmonic analyzer to
      `ToolInstancePolicy::multi` in `BuiltInToolCatalog.h`.
- [ ] 4.2 Rebuild `showToolsMenu` around an (item id → instance, action) table
      instead of `index × 4 + offset`:
      - "Open or focus";
      - "Open another", disabled with its reason when `mayOpenAnother` is false;
      - per-instance submenus once two or more are live.
- [ ] 4.3 Add `openAnotherInstance(toolId)`, which uses `nextInstanceId` and
      presents the instance where "Open or focus" would.
- [ ] 4.4 Add "Rename...", in the Tools menu's per-instance entry and in the
      tool's own **...** options menu (`ToolOptionsButton`, beside "Give
      feedback"). It opens an `AlertWindow` with one text field, prefilled with
      the current name. It stores the label in the `LiveTool` entry and in the
      active workspace, then refreshes the window title, tab label, dock header
      and menu.
- [ ] 4.5 `toolName` reads the label first, then `ToolInstanceLabel::defaultName`.
- [ ] 4.6 Capture writes `toolLabels`, and apply restores them before an
      instance is presented.
- [ ] 4.7 On restore, open audio-consuming instances in workspace order while
      `freeConsumerSlots()` allows, and leave the rest closed. Confirm the
      restoration code already tolerates an instance it leaves closed. If it
      doesn't, fix that here and record it in this file.

## 5. Verification

- [ ] 5.1 Add a `two-tuners-split` approved state (two tuners tiled, the second
      renamed) and its surface. Regenerate
      `docs/contracts/testcontrol/approved-states.json` by building
      `PracticeTakesTests`.
- [ ] 5.2 Capture `two-tuners-split`, `all-tools-tone` and `tuner-docked` (the
      restart surface) at the default, constrained and slim sizes, in both
      themes, and read every image.
- [ ] 5.3 Run `PracticeTakesTests` and `python tools/scripts/run_tests.py`.

## 6. Follow-ups and docs

- [ ] 6.1 File an issue for a state-only `AudioInputService` listener. The tuner
      and harmonic analyzer hold a FIFO slot they never drain, which halves the
      room for more instances.
- [ ] 6.2 Update the wiki: Tools ("Open another", renaming, the cap), Workspace
      and Settings, and the Features page row for #135. Add a picture from the
      `two-tuners-split` capture.
- [ ] 6.3 Update #135, removing the #134 dependency (maintainer decision
      2026-11-08) and linking this change. Tick #12's checklist when it closes.
