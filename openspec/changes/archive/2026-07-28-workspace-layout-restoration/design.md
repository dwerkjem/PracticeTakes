## Context

Practice Takes currently has three separate pieces of workspace state:

- `WorkspaceLayoutState` owns a pure recursive tree of leaves, tab groups, and splits.
- `WorkspaceToolState` owns whether each tool is closed, docked, or floating.
- JUCE components own mutable presentation details such as divider positions, selected tabs, and floating-window bounds.

The tree has no import/replace operation, split ratios are initialized to 50/50 whenever presentation components are rebuilt, and tab clicks do not update the tree's `activeTab`. Existing `SettingsPersistence` stores versioned scalar values in a JUCE XML `PropertiesFile`, including tuner settings and independent floating bounds, but it does not restore which tools are open or their dock topology. That same native file also contains feedback drafts, contact information, installation identity, and usage counters owned outside `AppSettings`, so copying the native settings file would be an unsafe export mechanism.

The change crosses pure layout logic, the JUCE shell, settings migration, file codecs, display recovery, and user-facing management workflows. Startup must continue to avoid synchronous audio initialization and must remain visually usable while restoring a useful first workspace.

## Goals / Non-Goals

**Goals:**

- Make the pure workspace model authoritative for topology, tab selection, and split ratios.
- Restore the latest active workspace and manage immutable built-ins plus named user snapshots.
- Recover deterministic usable layouts when tools are renamed, removed, duplicated, or saved on unavailable displays.
- Export an explicit allowlist of portable user preferences and workspace state.
- Validate imports completely before confirmation and apply them without partial live or persisted state.
- Preserve existing global-settings migration and corruption behavior.

**Non-Goals:**

- Cloud synchronization or automatic cross-device transfer.
- Merging an imported catalog with local named workspaces; import replaces the supported settings after confirmation.
- Exporting feedback contents, installation identity, usage history, audio recordings, live analysis history, logs, or benchmark data.
- Saving transient analysis samples or resuming an analyzer's live history.
- Guaranteeing that an older application can import a bundle written by a newer unsupported schema.
- Adding configurable settings to tools that do not currently expose any.

## Decisions

### Use typed snapshots with stable string tool identifiers

Introduce copyable, JUCE-independent workspace document types alongside the pure layout engine:

```text
WorkspaceCatalog
  version
  active: WorkspaceSnapshot
  activeSource: optional built-in or named ID
  named[]: { stable ID, display name, WorkspaceSnapshot }

WorkspaceSnapshot
  dockRoot: optional WorkspaceNode
  floating[]: { tool ID, bounds }
  focusedTool: optional tool ID
  toolSettings: map<tool ID, versioned settings payload>

WorkspaceNode
  leaf: tool ID
  tabs: ordered tool IDs + active tool ID
  split: orientation + first-pane ratio + two child nodes
```

Placement is canonical rather than duplicated: membership in `dockRoot` means docked, membership in `floating` means floating, and a registered tool absent from both is closed. This prevents a snapshot from claiming two presentations for one tool. Ratios are finite fractions of the child area excluding the divider and describe the first child.

A central tool registry maps stable IDs such as `tuner`, `spectrogram`, and `harmonic-analyzer` to `ToolType`, component construction, settings adapters, and historical aliases. Enum ordinals never cross the persistence boundary. Unknown settings payload fields are ignored when the payload version is supported, while an unsupported payload version falls back to that tool's defaults.

Alternative considered: serialize the current `WorkspaceLayoutState::Node` and enum integers directly. Rejected because the node is move-only, has no ratio, and integer identifiers make renames and reordered enums unsafe.

### Keep built-ins immutable and restore an independent working copy

Built-ins are code-defined snapshots with stable IDs and are not stored in user settings:

| Built-in | Initial arrangement |
|---|---|
| Pitch Practice | Tuner filling the docked workspace with general-instrument tuner settings |
| Vocal Warm-up | Tuner and Harmonic Analyzer in a 50/50 horizontal split with voice tuner settings |
| Spectrum Analysis | Spectrogram and Harmonic Analyzer in a 58/42 horizontal split |
| Performance Preparation | Tuner and a Spectrogram/Harmonic Analyzer tab group in a 50/50 horizontal split |

The 58/42 spectrum ratio stays near the current horizontal minimum-size limits at the default 1200-pixel window while giving the spectrogram the larger region. Actual presentation always clamps a requested ratio to the sizes available under current minimum pane constraints.

Applying a built-in or named workspace deep-copies it into `active`. Later interaction mutates only that working copy. `activeSource` supports menu selection and overwrite actions but does not make edits implicitly modify the source. Pitch Practice is used on first launch, explicit layout reset, and unrecoverable active state.

Named layouts use generated stable IDs independent of their names. Names are trimmed, 1-80 characters, and unique case-insensitively; built-in names are reserved. Save and rename collisions require explicit overwrite confirmation. Deleting a named source clears `activeSource` if needed but does not disturb the active working copy.

Alternative considered: persist only the selected preset and replay user actions. Rejected because arbitrary nested topology, ratios, floating geometry, and active tabs cannot be reconstructed reliably.

### Feed tab and divider changes back into the model

Extend split nodes with a ratio and add a validated whole-tree replacement API to `WorkspaceLayoutState`. `WorkspaceSplitPane` receives its initial ratio and reports user changes through a callback keyed to the corresponding model node. `WorkspaceTabbedComponent` reports selected tool changes so `activeTab` is updated. Floating-window move/resize, focus, presentation changes, and supported tool-setting changes update the active snapshot or are captured before a save/export.

Node callbacks must use stable node identity rather than raw pointers that are invalidated by tree replacement. A generated node ID or model-owned path is assigned during construction and resolved on callback. Rebuilding presentation components reads all ratios and active tabs from the model, so a rebuild no longer resets user adjustments.

Workspace application is coordinated as one shell operation: detach existing presentations, replace the validated model, create each required tool once, apply its settings, construct docked and floating presentations, then rebuild the workspace container once. Automatic settings saving is suppressed during this operation.

Alternative considered: inspect transient JUCE component geometry only during shutdown. Rejected because containers are rebuilt during normal interaction and would already have discarded prior ratios or tab choices.

### Normalize untrusted or obsolete snapshots before application

All persisted and imported snapshots pass through the same normalizer:

1. Resolve current IDs and explicit historical aliases.
2. Traverse the dock tree in order, dropping unknown IDs and later duplicates.
3. Collapse empty splits, promote a sole surviving split child, and reduce one-item tab groups to leaves.
4. Replace an invalid active tab with the first surviving tab.
5. Accept only finite ratios and clamp them to `[0.1, 0.9]`; presentation applies stricter size-dependent limits.
6. Process floating entries in order, dropping unknown IDs and any tool already placed; constrain valid bounds to a connected display's user area.
7. Drop settings for unknown tools and validate known tool payloads independently.
8. Clear an unavailable focused tool or select the first visible surviving tool.

Documents are limited to 4 MiB, 128 named layouts, a dock-tree depth of 32, and bounded string and collection lengths. If no visible known tool survives in the active snapshot, replace it with Pitch Practice. A malformed named layout is omitted without invalidating other named layouts; duplicate or reserved imported names are resolved by the version-specific migration before validation or rejected with a precise error.

Alternative considered: reject the entire settings store for any bad workspace field. Rejected because issue #23 explicitly requires startup recovery and independently valid global settings must survive.

### Store a versioned workspace catalog inside existing settings persistence

Add one owned `workspace.catalog` value containing compact JSON with its own document version and increment the application settings schema. `AppSettings::State` carries the typed catalog after decoding rather than exposing raw JSON to the shell.

Migration from the current schema creates an active Pitch Practice snapshot, seeds its tuner settings from the existing tuner values, and retains valid legacy floating bounds for future floating presentation. Existing theme, audio, fullscreen, and other valid preferences migrate unchanged. Legacy layout keys can be read during migration and removed when the migrated state is successfully saved.

The workspace document version is independent from the native settings schema and the portable bundle schema. This lets internal storage, workspace shape, and transfer compatibility evolve without coupling every migration.

Alternative considered: flatten every tree field into `PropertiesFile` keys. Rejected because recursive trees, named collections, and future per-tool payloads become fragile and difficult to migrate that way.

### Export a JSON bundle from an explicit allowlist

Use a UTF-8 JSON file with the suggested `.ptsettings` extension and a root format marker:

```json
{
  "format": "practice-takes-settings",
  "schemaVersion": 1,
  "applicationVersion": "...",
  "settings": {
    "appearance": {},
    "audio": {},
    "window": {},
    "feedback": {},
    "workspaces": {}
  }
}
```

The transfer codec receives typed `AppSettings::State` plus the user-controlled feedback-invitation preference. It never enumerates or copies arbitrary native `PropertySet` keys. Tool settings are carried in active and named workspace snapshots. Built-in definitions are omitted because the importing version supplies them.

The audio section may contain JUCE's saved device state because device selection is an explicit user preference, but import treats it as a request and falls back safely if unavailable. The feedback section contains only `invitationsDisabled`; it excludes drafts, contact email, context, installation ID, successful-use count, and invitation history.

Export serializes to a temporary file in the destination directory and atomically replaces the chosen destination only after serialization and flush succeed. Cancelling the asynchronous file chooser performs no write.

Alternative considered: export the native XML settings file. Rejected because it leaks unrelated private and operational properties, exposes implementation-specific keys, and cannot provide a stable public compatibility contract.

### Import through a staged replacement transaction

Import follows a fixed pipeline:

```text
choose file
  -> bounded read
  -> format/schema check
  -> migrate to current transfer schema
  -> typed decode
  -> workspace normalization and full validation
  -> replacement summary and confirmation
  -> capture pre-import typed state
  -> suppress automatic saves
  -> apply candidate to live services and workspace
  -> atomically persist candidate merged with untouched non-transfer properties
  -> commit and resume automatic saves
```

The confirmation states that supported preferences and all named workspaces will be replaced and summarizes the imported theme, audio preference, active workspace, and named-layout count. There is no implicit merge in this version.

The importer preserves local properties outside its allowlist, including feedback drafts and installation identity. If live application fails, the coordinator reapplies the captured prior typed state and never writes the candidate. If atomic persistence fails after live application, it restores the prior live state; the previous on-disk file remains intact. Audio-device unavailability and off-screen geometry are recoverable normalization outcomes, not transaction failures. A successful import updates the running UI, workspace, audio preference, and settings controls immediately.

Alternative considered: write imported properties first and require restart. Rejected because a crash or later live-application failure could leave partially surprising behavior, and the requirement calls for immediate effect.

### Put workspace operations near tools and transfer operations near settings

Add `Tools > Workspaces` with built-in and named choices plus Save As, Overwrite, Rename, Delete, and Reset actions. Destructive overwrite and delete actions require confirmation. The existing Settings surface exposes Import Settings and Export Settings commands using asynchronous native file choosers; import uses a second confirmation after validation.

This separates frequent workspace switching from application-level backup and transfer. Errors are reported in focused dialogs with no visible partial state.

### Test codecs and normalization below the JUCE shell

Add focused tests for snapshot replacement, ratio/tab callbacks, every built-in, named-layout lifecycle, alias and missing-tool recovery, display-bound normalization, settings migration, transfer round trips, allowlist privacy, older/newer bundle handling, bounded malformed input, cancellation, and transaction rollback with a failing persistence fake.

Shell-level tests or deterministic automation cover complex restart restoration and immediate import application. Exact process-owned golden validation, with the pointer parked and the title bar excluded as established by the repository tooling, verifies first-launch and restored usability. Launch timing is recorded to catch synchronous restoration or audio regressions.

## Risks / Trade-offs

- [Recursive data and callbacks can retain stale node references] -> Use stable node IDs or resolved paths and invalidate callbacks when containers are rebuilt.
- [Minimum pane sizes can prevent exact ratio reproduction on smaller windows] -> Persist the requested normalized ratio, clamp only presentation geometry, and preserve topology.
- [Applying imported audio state can fail on another machine] -> Treat device restoration as recoverable and retain the imported portable preferences while selecting a default or no-input state.
- [A settings export could expose private data if future properties are added] -> Encode only typed allowlisted fields; never iterate the native property store.
- [A failed import could leave live and persisted state different] -> Centralize application in a transaction coordinator with auto-save suppression, a complete pre-import snapshot, atomic persistence, and tested rollback.
- [Opening the default workspace can regress first usable paint] -> Construct no audio device synchronously, retain deferred audio initialization, and measure exact-golden usable time after integration.
- [Workspace-scoped tuner settings change when presets are applied] -> Make this explicit in the workspace model; global appearance and audio preferences remain outside snapshots.
- [Replacing rather than merging imported named layouts can discard local layouts] -> State replacement clearly in confirmation and make export the backup path before import.

## Migration Plan

1. Add typed workspace documents, codecs, normalizer, built-ins, and unit tests without changing startup behavior.
2. Add ratios and active-tab mutation to the authoritative layout model and wire presentation callbacks.
3. Increment the native settings schema and migrate current scalar layout/tuner state into a workspace catalog.
4. Restore the active workspace at startup and add named workspace management.
5. Add allowlisted transfer codecs, transaction coordination, and import/export UI.
6. Update documentation and run unit, integration, exact-golden visual, and launch-time validation.

If migration fails, existing corruption recovery retains independently valid defaults and uses Pitch Practice. A release rollback sees the newer native schema and, following existing behavior, leaves it untouched rather than overwriting it; returning to the new release restores the migrated settings.

## Open Questions

None. The initial implementation intentionally uses replacement import semantics, Pitch Practice as the default, workspace-scoped tuner settings, and the `.ptsettings` JSON format; future changes can add selective merge or additional tool-setting adapters without changing these core boundaries.
