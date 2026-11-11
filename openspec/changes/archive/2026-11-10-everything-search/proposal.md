## Why

The application has grown past what its menus show at a glance. There are five
Settings panels of controls, eight tools, a Tools menu of arrangements and
workspaces, and now a library of songs and takes. A reader looking for "where
do I set the reference pitch" or "that take from Tuesday" has to know where it
lives. The maintainer asked for one search over all of it, backed by
**Tantivy**, opened from a magnifying glass (2026-11-10).

## What Changes

- **A search index**: Tantivy, in a small Rust crate (`src/search`) behind a C
  interface. CMake builds it through Corrosion and links it into the
  application and the tests. The index lives in memory and is rebuilt from the
  application's own state, so there is no file on disk to version.
- **What it finds**:
  - every control in Settings, by its label, its tooltip and its choices;
  - each Settings panel;
  - the tools, by name and alias;
  - the menus' commands (open a score, the library, theme, demo mode,
    import and export settings, arrangements, clear the target, feedback,
    play, stop and record);
  - the workspaces;
  - the library's songs and takes;
  - the open score's sung lines and its bars.
- **The magnifying glass** in the title strip, always shown, and **Ctrl+K**
  (Cmd+K; Ctrl+F too) from anywhere. Both open a search bar over the
  workspace. It searches as the reader types, and lists the best twelve
  matches, each marked with its kind and where it is.
- **Choosing a result acts on it**:
  - a setting opens Settings on its panel with the control ringed in violet;
  - a tool opens;
  - a command runs;
  - a song opens;
  - a take is reviewed;
  - a workspace is applied;
  - a lyric line or a bar moves the practice clock there.
- **Forgiving matching**:
  - every word must match, and the last may be half-typed;
  - a word of four letters or more may be one letter out;
  - case and accents are ignored;
  - nothing a reader types is query syntax, so `C#` or a stray quote cannot
    break it.
- **A Rust toolchain becomes a build requirement** on every platform: cargo
  and rustc 1.86 or newer. The CI jobs get it from a composite action; the
  Linux dependency check warns when it is missing.

## Capabilities

### New Capabilities
- `search`: the index, what is in it and when it is rebuilt, matching, the
  search bar and what choosing a result does.
- `settings-search`: how Settings' controls are found from the catalogue, and
  revealed when chosen.

## Impact

- **New**:
  - `src/search` (Rust: the index, its C interface and its tests);
  - `src/platform/search` (the C++ wrapper `SearchIndex`, and `ScoreSearch`,
    which turns a score into searchable phrases and bars);
  - `src/application/search` (`SearchCatalogue`, everything else as
    documents);
  - `src/application/shell/ui/search` (the popup and the shell's half);
  - `tools/cmake/Search.cmake`;
  - `.github/actions/rust-toolchain`.
- **Changed**:
  - the title strip gains the magnifying glass;
  - `SettingsWindow` can reveal a control by its words;
  - `Localisation` can list its keys by prefix;
  - the test target links the crate;
  - every CI job that configures CMake installs Rust when the runner lacks it.
- **Build**: a first build compiles Tantivy (about a minute) and fetches its
  crates from crates.io, pinned by `src/search/Cargo.lock`. The application
  grows by about 3 MB.
- **Audio thread**: untouched. Every index call is on the message thread, and
  the library listing that feeds it is on the library's worker.
