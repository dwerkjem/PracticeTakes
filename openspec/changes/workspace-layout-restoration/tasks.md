## 1. Workspace Documents and Recovery

- [x] 1.1 Add failing tests for typed workspace snapshots covering nested splits, ratios, tab order and selection, floating bounds, focus, supported tool settings, and canonical closed/docked/floating placement
- [x] 1.2 Implement copyable workspace snapshot and catalog types with stable string IDs, generated named-workspace IDs, bounded names, and versioned per-tool settings payloads
- [x] 1.3 Add failing tests for tool-ID aliases, missing and duplicate tools, malformed branches, invalid ratios and active tabs, unavailable displays, depth and collection limits, and empty-layout fallback
- [x] 1.4 Implement the central tool registry and workspace normalizer, including alias resolution, branch collapse, ratio constraints, display-bound recovery, and Pitch Practice fallback
- [x] 1.5 Add tests that assert the exact tool arrangements, ratios, active tabs, and tuner settings of all four immutable built-in workspaces
- [x] 1.6 Implement Vocal Warm-up, Pitch Practice, Spectrum Analysis, and Performance Preparation as immutable code-defined snapshots

## 2. Authoritative Live Workspace State

- [x] 2.1 Add failing `WorkspaceLayoutState` tests for whole-tree replacement, split-ratio mutation, active-tab mutation, stable node identity, and validation that prevents duplicate tool placement
- [x] 2.2 Extend `WorkspaceLayoutState` nodes and commands with split ratios, stable node IDs, selected-tab updates, and validated snapshot replacement
- [x] 2.3 Add component tests for restoring a divider ratio, reporting a dragged divider, restoring an active tab, and reporting a clicked tab without regressing tab dragging
- [x] 2.4 Wire `WorkspaceSplitPane` and `WorkspaceTabbedComponent` callbacks to the authoritative model using stable node identities, and rebuild presentation from stored ratios and active tabs
- [x] 2.5 Add tests for capturing floating move/resize, focused tool, presentation changes, and tuner settings into the active snapshot
- [x] 2.6 Implement shell capture adapters for tool presentation, floating bounds, focus, and supported per-tool settings
- [x] 2.7 Add tests that applying a snapshot creates every tool at most once, rebuilds docked and floating presentation once, and preserves global theme, audio, fullscreen, and feedback preferences
- [x] 2.8 Implement the coordinated live workspace apply operation with temporary auto-save suppression and one final presentation rebuild

## 3. Workspace Persistence and Startup

- [x] 3.1 Add codec tests for deterministic workspace-catalog JSON round trips, malformed documents, unsupported versions, bounded input, recoverable named-layout failures, and unrecoverable active-layout fallback
- [x] 3.2 Implement the versioned workspace catalog codec and add it as one owned value in `AppSettings::State`
- [x] 3.3 Add migration tests from settings schema 4 that preserve valid global values, tuner settings, and legacy floating bounds while creating a Pitch Practice active workspace
- [x] 3.4 Increment the native settings schema and implement migration, owned-key storage, corruption recovery, and cleanup of successfully migrated legacy layout keys
- [ ] 3.5 Add startup and shutdown tests for first launch, active-session restart, selected tabs, divider ratios, floating windows, disconnected displays, missing tools, and deferred audio initialization
- [ ] 3.6 Restore the active workspace during shell startup, persist active changes through normal settings saves, and make reset replace only the active workspace with Pitch Practice

## 4. Named Workspace Management

- [ ] 4.1 Add service tests for save, apply, rename, overwrite, delete, case-insensitive name collisions, reserved built-in names, cancellation, and deletion of the active source
- [ ] 4.2 Implement named-workspace lifecycle operations over stable IDs and independent working copies
- [ ] 4.3 Add `Tools > Workspaces` entries for built-ins and named workspaces plus Save As, Overwrite, Rename, Delete, and Reset commands
- [ ] 4.4 Add name-entry, overwrite, delete, and reset confirmations with accessible labels, clear disabled states, and focused success or error reporting

## 5. Portable Settings Bundle

- [ ] 5.1 Add transfer-codec tests for a deterministic `.ptsettings` JSON round trip containing appearance, audio, window, feedback-invitation preference, tool settings, active workspace, and named workspaces
- [ ] 5.2 Add privacy tests proving export excludes feedback drafts, contact information, installation identity, use counts, invitation history, unknown native properties, logs, benchmarks, and analysis data
- [ ] 5.3 Implement the bounded, versioned settings-bundle encoder from explicit typed allowlisted fields without enumerating the native `PropertySet`
- [ ] 5.4 Add decoder tests for malformed input, wrong format markers, missing or invalid required fields, unknown fields, supported older migrations, unsupported newer schemas, document-size limits, and duplicate or conflicting workspace names
- [ ] 5.5 Implement transfer-schema migration, typed decoding, workspace normalization, actionable validation errors, and safe audio-device and display fallback
- [ ] 5.6 Add file-operation tests for chooser cancellation, temporary-file cleanup, write failure, and atomic destination replacement
- [ ] 5.7 Implement asynchronous `.ptsettings` export with a native chooser, temporary sibling file, flush, atomic replacement, and user-visible result reporting

## 6. Transactional Settings Import

- [ ] 6.1 Add transaction tests for pre-confirmation non-mutation, confirmation cancellation, complete replacement, preservation of non-transfer native properties, immediate live application, and restart persistence
- [ ] 6.2 Add failure-injection tests proving live-apply and persistence failures restore the complete prior live state while leaving the prior settings file intact
- [ ] 6.3 Implement the import coordinator with bounded read, migration and validation into temporary state, pre-import snapshot capture, auto-save suppression, atomic persistence, and rollback
- [ ] 6.4 Add Settings controls for Import Settings and Export Settings using asynchronous native choosers and keyboard-accessible actions
- [ ] 6.5 Show a validated replacement summary with imported theme, audio preference, active workspace, and named-workspace count, then require explicit confirmation before import
- [ ] 6.6 Apply confirmed appearance, audio preference, fullscreen behavior, feedback-invitation preference, tool settings, and workspace catalog immediately while preserving feedback drafts, identity, counters, and history

## 7. Documentation and Regression Validation

- [ ] 7.1 Update settings and user documentation with built-in and named workspace behavior, reset semantics, `.ptsettings` export contents and exclusions, replacement import behavior, compatibility errors, and unavailable-device fallback
- [ ] 7.2 Build the application and run the focused workspace, settings-persistence, transfer-codec, migration, and transaction test suites
- [ ] 7.3 Run the complete CTest suite and resolve only regressions caused by this change
- [ ] 7.4 Generate approved first-launch and representative restored-workspace goldens after a 7-second settle with the pointer parked and title-bar pixels excluded
- [ ] 7.5 Run process-owned exact-golden launch validation against those goldens, verify a visible responsive workspace, and record time-to-exact-match against the current 414.134 ms usable-time baseline
- [ ] 7.6 Verify import and named-workspace workflows visually at desktop and constrained window sizes, including confirmation, validation error, rollback error, long names, disconnected-display recovery, and pointer-insensitive comparisons
