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
| `readsAudioSamples` | Set when each instance reads samples through its own `AudioInputService` FIFO (`addListener`). The shell checks for a free stream before creating one, so a tool that reads samples must say so, and must be `multi` (a test pins that). A tool that only needs the input's state — muted, disconnected — uses `addStateListener` and leaves this false. |

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

A tool may add its own entries to its options menu (the `...` button) through
`optionsMenuEntries()`, a list of `ToolComponent::MenuEntry`: a label, an action
and a tick, shown above the shell's own items. An entry can also be disabled
(`enabled = false`), shown but not choosable, or be a heading whose `submenu`
holds more entries; the Score View's "Parts" is both. The shell numbers the
choosable entries depth-first (`application/tools/ToolMenuEntries.h`) and asks
the tool for its entries again when one is chosen, so entries built from the
tool's own state never run a stale action. An entry that sets neither field
behaves as entries always have.

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
   and the questions a reviewer should answer about the picture. Only the state
   and the questions: which tools it opens, how, and in which window come from
   the generated approved-states manifest, which building `PracticeTakesTests`
   rewrites after steps 1 and 2; commit it with them. Nothing enforces the surface itself: skip it and the tool is simply
   never photographed.

`the built-in catalog declares every shipped tool` also pins the number of
tools. Bumping it is deliberate — it is what makes adding a tool to the roster
something a reviewer sees — but it is a fourth edit, so expect it.

## Removing a tool

A tool's id is written into saved workspaces, so it never stops resolving. The
Target Note tool was the first to go (`system-wide-target-note`), and it set
the path:

1. **Alias every id it had onto the tool that takes its place**, in that tool's
   catalog entry: the old id and the old tool's own aliases, permanently. A
   workspace that named it opens the replacement where the old tool was. Its
   instance policy now applies to those ids: an alias onto a multi-instance
   tool opens another instance, rather than dropping a duplicate.
2. **Remove its catalog entry and factory**, and its component.
3. **Remove its approved states and surfaces**, take it out of
   `all-tools-docked`, and bump the pinned tool count down. Regenerate
   `approved-states.json` by building `PracticeTakesTests`.
4. **Move anything still worth keeping** to where its new users can reach it.
   The Target Note tool's selection logic and tests moved to
   `src/platform/score/`, because the shell uses them now.
5. **Add a test** that the old ids resolve to the replacement.

## Instance policy

A **tool** is a kind of tool; an **instance** is one live copy.

- `single` — opening the tool while it is already open focuses the existing
  instance. Its instance id is always exactly the tool id. The score view and
  the target note tool are `single`: every copy would show the same open score.
- `multi` — **Tools → *tool* → Open another** creates another instance, with
  the lowest ordinal not currently live. The second instance's id is
  `"loudness-meter#2"`. "Open or focus" still focuses the first live instance;
  only "Open another" duplicates. The tuner, spectrogram and harmonic analyzer
  are `multi`.

Two instances share one catalog entry — one name, one factory — but get their
own settings payload, keyed by instance id, so they can be configured
differently.

Each instance is named by `MainComponent::toolName`: the user's label when they
have renamed it (**Rename...**, in the Tools menu or the tool's **...** menu,
stored as the workspace's optional `toolLabels`), otherwise the display name,
with the ordinal after the first ("Tuner 2"). The naming rules are in
`ToolInstanceLabel.h`.

There are at most six copies of any tool (`maximumInstancesPerTool`). A tool
that reads samples also needs one of `AudioInputService`'s sixteen sample
streams. "Open another" shows which limit applies; out of streams, choosing it
explains who holds them (`AudioStreamUse.h`). See
`openspec/changes/archive/*-multi-instance-tools/design.md` and
`openspec/changes/archive/*-audio-stream-accounting/design.md`.

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
- [ ] `docs/contracts/testcontrol/approved-states.json` committed with the
      state change (a local test build regenerates it).
- [ ] The tool count in `ToolCatalogTests` bumped.
- [ ] A surface in `tools/scripts/testing_suite/surfaces.py` names the state, so
      the tool is actually photographed.
