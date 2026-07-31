# Design and architecture review checklist

This is a review checklist, not a design tutorial. It exists so pull request
reviewers have concrete, checkable questions for design and architecture
quality — the things `clang-format`/`clang-tidy` (see
[Code quality](QUALITY.md)) cannot check. Use it alongside
[Architecture](ARCHITECTURE.md) (what the current system looks like) and
[Code style](CODE_STYLE.md) (line-level conventions).

Not every item applies to every PR. Use judgment: a one-line bug fix does not
need a layering review. A new tool, a new shared service, or a change that
touches ownership/lifetime does.

## Source layering

- [ ] New code lives in the layer that matches its role: `src/bootstrap`
      (app entry/top-level window), `src/application` (shell, configuration,
      theme), `src/features` (user-facing tools), `src/services` (shared
      infrastructure). See [Architecture § Source organization](ARCHITECTURE.md#source-organization).
- [ ] A `src/features/*` tool does not reach into another tool's internals.
      Shared behavior belongs in `src/services` or `src/application`, not in
      one feature depending directly on another.
- [ ] A new subdirectory is justified by more than one file, or by a
      responsibility that's clearly distinct from its siblings — avoid
      one-off folders that just add navigation overhead.

## Ownership and lifetime

- [ ] Every new owned object has exactly one clear owner, expressed with
      `std::unique_ptr` or equivalent RAII, per
      [Code style § Ownership](CODE_STYLE.md#ownership).
- [ ] Shared, long-lived services (audio device manager, look-and-feel,
      `AudioInputService`) are referenced, not duplicated. Check whether a
      new feature actually needs its own instance of something that should
      be shared (e.g., don't open a second audio device).
- [ ] Presentation containers (`DockedToolPanel`, `ToolWindow`, tab
      components) never own the tool component — reparenting between
      presentation modes must not destroy or recreate application state. See
      [Architecture § Tool workspace](ARCHITECTURE.md#tool-workspace).
- [ ] Destruction order is deliberate where non-owning raw pointers are
      involved: detach references before destroying the objects they point
      into (see the `MainComponentLifecycle.cpp` destructor ordering note in
      repo memory / architecture).

## Audio-thread boundary

- [ ] Nothing reachable from `audioDeviceIOCallbackWithContext` (or any
      real-time callback) allocates, locks, blocks, touches files/network,
      logs, or updates UI. See
      [Architecture § Audio-thread boundary](ARCHITECTURE.md#audio-thread-boundary)
      and [Code style § Real-time audio rules](CODE_STYLE.md#real-time-audio-rules).
- [ ] New audio consumers drain their own preallocated FIFO from a
      message-thread timer; a slow consumer cannot block capture or another
      consumer.
- [ ] Format/state changes (sample rate, device start/stop) are communicated
      via atomics read on the timer, not shared mutable state touched from
      both threads without synchronization.

## Coupling and testability

- [ ] Pure logic (state machines, layout trees, policy decisions) is
      separated from JUCE `Component`/UI code so it can be unit tested
      without a display — e.g. `ui/workspace/model/WorkspaceLayoutState.h`
      has no JUCE dependency and is covered by
      `tests/application/shell/ui/workspace/model/WorkspaceLayoutStateTests.cpp`.
      New non-trivial logic should follow the same split rather than being
      embedded directly in a `Component` subclass.
- [ ] A grep for a removed/renamed member or type covers the whole `src/`
      tree, not just files that mention the owning class name nearby —
      cross-file references can exist without mentioning the class (bit us
      once in `MainComponentLifecycle.cpp`).
- [ ] New cross-cutting UI behavior (drag/drop, theming, menus) reuses the
      existing single registration point (`allToolTypes`, the
      `beginToolDrag`/`draggedTool` contract, etc.) instead of adding a
      parallel per-tool branch that has to be kept in sync by hand.

## Scope and consistency

- [ ] The change matches an existing pattern already documented in
      [Architecture](ARCHITECTURE.md) unless it's deliberately introducing a
      new pattern — and if so, that's called out in the PR description so
      reviewers know it's intentional, not an oversight.
- [ ] Public API surface added to a widely-used type (e.g. `MainComponent`)
      is the minimum needed; prefer extending an existing method's contract
      (e.g. an optional parameter with a default) over adding a parallel
      overload when the two would otherwise duplicate logic.
- [ ] Non-trivial or non-obvious design decisions (why this approach over an
      alternative) are captured in a code comment, PR description, or an
      OpenSpec `design.md` — not left implicit for future readers to
      reverse-engineer.

## When to write more than a checklist pass

Open an OpenSpec change (`proposal.md` + `design.md`) instead of relying on
this checklist alone when a change:

- touches more than one of the layers above,
- introduces a new shared service or ownership pattern,
- changes the audio-thread contract, or
- is large/ambiguous enough that a reviewer would benefit from written
  rationale before reading the diff.

Small, single-layer changes that clearly follow an existing pattern don't
need a design doc — this checklist is enough.
