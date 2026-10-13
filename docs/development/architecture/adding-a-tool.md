# Adding a tool

A tool is declared to the **shell** in two places and nowhere else. Nothing
that coordinates the window needs to change: the Tools menu, docking, floating,
tiling, tabs, drag and drop, theming, workspace capture, and workspace restore
all discover tools through the registry.

Three further things are not the shell and do have to be edited, because a tool
nobody can verify is not finished — see step 4. Adding `target-note` found them
by failing three tests after the tool already worked, which is later than anyone
should learn it.

## 1. Declare it

Add one entry to `builtInToolCatalog()` in
`src/application/tools/BuiltInToolCatalog.h`:

```cpp
ToolDefinition{
    "loudness-meter",       // id
    "Loudness Meter",       // displayName
    {},                     // aliases
    ToolInstancePolicy::single,
    std::nullopt,           // settingsVersion
    {900, 620}},            // preferredSize
```

| Field | Meaning |
| --- | --- |
| `id` | Stable identifier written into saved workspaces. Never change it — add an alias instead. May not contain `#`. |
| `displayName` | What the user sees in the menu, the dock header, and the window title. |
| `aliases` | Identifiers this tool used to be saved under, so an older workspace still resolves after a rename. |
| `instancePolicy` | `single` or `multi`. See below. |
| `settingsVersion` | Set only if the tool persists settings. Bump it when the payload format changes; stored payloads at any other version are discarded and the tool starts at its defaults. |
| `preferredSize` | Opening size of the floating window. |

## 2. Build it

Add one factory to `builtInToolRegistry()` in
`src/application/tools/BuiltInTools.cpp`:

```cpp
{"loudness-meter",
 [](const ToolServices& services)
 { return std::make_unique<LoudnessMeterComponent>(services.audio); }},
```

The two halves are checked against each other at startup: a catalog entry with
no factory, or a factory with no catalog entry, trips the `jassert` in
`builtInToolRegistry()` rather than shipping a menu item that cannot open.

## 3. Write the component

Derive from `ToolComponent` (`src/application/tools/ToolComponent.h`):

```cpp
class LoudnessMeterComponent final : public ToolComponent, /* ... */
{
  public:
    void setTheme(Theme theme) override;
    void resetToDefaults() override;

    // Only if the tool persists settings.
    [[nodiscard]] std::optional<ToolSettingsPayload> captureSettings() const override;
    void applySettings(const ToolSettingsPayload& payload) override;
};
```

`setTheme` and `resetToDefaults` are required. The settings pair defaults to a
tool that persists nothing and always starts at its defaults, which is what the
spectrogram and harmonic analyzer do. A tool that overrides them must also
declare a `settingsVersion`, and must check the version it is handed rather than
trusting it — see `TunerComponent` and `TunerSettingsCodec` for the pattern of
keeping the format with the tool that understands it.

Reach shared services only through `ToolServices`. A `src/features/*` tool must
never reach into the shell or into another tool.

Finally, add the sources to `PRACTICE_TAKES_SOURCES` in
`tools/cmake/Sources.cmake`, and any tests to `PRACTICE_TAKES_TEST_SOURCES`
beside it.

## 4. Make it verifiable

A tool's `Component` code is outside `PracticeTakesTests` by design, so the
manual capture suite is the only thing that ever looks at it. Three edits, two
of which fail a test if you skip them:

1. **An approved state naming the tool**, in
   `src/application/testcontrol/ApprovedWindowStates.cpp`. Without one the tool
   cannot be put on screen by the harness at all, and
   `every tool is reachable in at least one state` fails. A tool that shows
   nothing until something else has happened wants a second state for its empty
   case — that is what most sessions see.
2. **`all-tools-docked` updated** to include the new tool, in the same file.
   `every tool can be open at once` compares that state against the catalog, so
   it fails otherwise.
3. **A surface in `tools/scripts/testing_suite/surfaces.py`**, naming the state
   and the questions a reviewer should answer about the picture. Nothing
   enforces this one: skip it and the tool is simply never photographed.

`the built-in catalog declares every shipped tool` also pins the number of
tools. Bumping it is deliberate — it is what makes adding a tool to the roster
something a reviewer sees — but it is a fourth edit, so expect it.

## Instance policy

A **tool** is a kind of tool; an **instance** is one live copy. They are
separate concepts even though every shipped tool is `single`.

- `single` — opening the tool while it is already open focuses the existing
  instance. Its instance id is always exactly the tool id.
- `multi` — opening it again creates another instance, with the lowest ordinal
  not currently live. The second instance's id is `"loudness-meter#2"`.

Two instances share one catalog entry — one name, one factory — but get their
own settings payload, keyed by instance id, so they can be configured
differently.

**`multi` is declared and enforced, but no shipped tool uses it.** It is
deliberately not exercised in production; the branch is kept alive by
`ToolLifecycleTests`. Before shipping a genuinely multi-instance tool, read
`openspec/changes/archive/*/tool-registry-contract/design.md` — the deduplication
in `WorkspaceNormalizer` and `WorkspaceSnapshotApply` already keys on instance
ids, so the remaining work is display naming and whatever UI offers the
duplicate action, not the workspace model.

## Lifecycle

The shell takes an instance through:

1. **create** — the factory runs with `ToolServices`; the registry then applies
   the current theme.
2. **restore settings** — `applySettings`, if a payload was stored for this
   instance.
3. **attach** — the component is placed into a `DockedToolPanel` or a
   `ToolWindow`. Neither owns it.
4. **move** — detach and reattach when the user docks or floats it. The same
   object is reused, so analysis state and audio registration survive.
5. **capture settings** — `captureSettings` on close and on every workspace
   capture.
6. **destroy** — the component is released.

Tools are destroyed before the services they borrowed. That is guaranteed
structurally: `MainComponent` declares `liveTools` after `audioInputService` and
`appLookAndFeel`, so reverse-order member destruction tears the tools down
first. Do not move that declaration.

## Checklist

- [ ] Catalog entry added.
- [ ] Factory added.
- [ ] Component derives from `ToolComponent`.
- [ ] `settingsVersion` declared if — and only if — the component overrides the
      settings pair.
- [ ] Sources added to `tools/cmake/Sources.cmake`.
- [ ] Analysis runs on a message-thread timer draining its own FIFO, never on
      the audio callback — see
      [`audio-thread-safety.md`](../performance/audio-thread-safety.md).
- [ ] An approved state in `src/application/testcontrol/ApprovedWindowStates.cpp`
      names the tool, so it can be manually verified.
- [ ] `all-tools-docked` includes the tool.
- [ ] The tool count in `ToolCatalogTests` bumped.
- [ ] A surface in `tools/scripts/testing_suite/surfaces.py` names the state, so
      the tool is actually photographed.
