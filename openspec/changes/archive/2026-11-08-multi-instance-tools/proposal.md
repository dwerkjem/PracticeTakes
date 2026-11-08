## Why

You can't open a second tuner beside the first, or two spectrograms to compare
settings. Issue #135 asks for it. `tool-registry-contract` (archived 2026-08-03)
built every seam this needs and stopped short of using them:

- instance ids already serialise as `tool#ordinal`;
- the workspace dedup checks already key on instance id;
- `ToolInstanceAllocator` already hands a multi-instance tool its lowest free
  ordinal;
- `WorkspaceNormalizer` already drops a duplicate only for a *single*-instance
  tool;
- `MainComponent::toolName` already names a second instance "Tuner 2".

No shipped tool declares `multi`, and nothing in the menu can ask for another
instance, so none of it is reachable.

The issue said it was blocked by #134 (tool categories), so that the Tools menu
would stay navigable once a tool can appear several times. #134 has since moved
to 1.0. On 2026-11-08 the maintainer dropped the dependency: with five tools, a
plain Tools menu stays navigable. That puts this change in the Foundation
release, as #12 plans.

## What Changes

- **The three microphone tools become multi-instance.** These are the tuner,
  the spectrogram and the harmonic analyzer. Score View and Target Note stay
  single-instance: both read the one open score, and a second copy shows the
  same thing.
- **"Open another"** joins a multi-instance tool's submenu in **Tools**. It is
  distinct from "Open or focus", which keeps its meaning: focus the first live
  instance, or open one.
- **Each live instance gets its own submenu entry**, under its name, with
  focus, dock, float, rename and close. A tool with one live instance looks
  exactly as it does today.
- **Instances can be renamed.** The label is stored in the workspace as an
  optional per-instance field. A workspace with no labels serialises exactly as
  today, and the catalog stays at version 1.
- **A cap on microphone instances.** `AudioInputService` has eight consumer
  slots, preallocated, which the audio callback walks in fixed time. "Open
  another" is disabled, and says why, when no slot is free. The audio-thread
  contract is unchanged.
- **Window titles, tab labels and menu entries** use the instance's label,
  falling back to "Tuner", "Tuner 2" and so on.
- **The tool-registry spec changes.** The rule that every shipped tool is
  single-instance is lifted, and the multi-instance rules are added.

Not in scope: tool categories and favourites (#134), several inputs (a
spectrogram per microphone needs more than one input device, which the
application doesn't have), and multi-instance Score View.

## Capabilities

### New Capabilities

<!-- None. -->

### Modified Capabilities

- `tool-registry`: the instance-policy requirement no longer requires every
  shipped tool to be single-instance. New requirements cover opening another
  instance, naming and renaming instances, the cap on instances that take an
  audio consumer, and the round trip of several instances of one tool.

## Impact

**Code**

- `src/application/tools/BuiltInToolCatalog.h`: three policies flip to
  `multi`.
- `src/application/shell/ui/workspace/shell/MainComponentWorkspaceMenu.cpp`:
  per-instance entries, "Open another", and "Rename...".
- `src/application/shell/ui/workspace/shell/MainComponentWorkspacePresentation.cpp`:
  `toolName` reads the label; a new `openAnotherInstance`.
- `src/application/shell/ui/workspace/model/WorkspaceDocuments.h`,
  `WorkspaceCatalogCodec.cpp`, `WorkspaceSnapshotCapture.h` and
  `WorkspaceSnapshotApply.h`: an optional `toolLabels` map, keyed by instance id.
- `src/platform/audio/AudioInputService.h`: a message-thread query for free
  consumer slots. The callback and the slot array are unchanged.
- A pure `ToolInstanceLabel` helper (default names, label validation) beside
  `ToolInstanceAllocator`, tested headless.

**Persistence**

- The catalog stays at version 1. `toolLabels` is optional, and an older
  application ignores it, because the decoder reads properties by name.
- A workspace saved with "Tuner 2" in it, opened by an older version, drops the
  second instance through the existing single-instance normalisation. Nothing
  fails.

**Tests**

- Allocator, label and normalizer tests for the `multi` branch with the real
  catalog, not only the fake tool that covers it today.
- Codec round-trip with and without `toolLabels`.
- The existing single-instance workspace and `.ptsettings` fixtures must load
  unchanged.
- A new testing-suite surface, `two-tuners-split`.

**Docs**

- The wiki's Tools and Workspace and Settings pages, and the Features page row
  for #135.
