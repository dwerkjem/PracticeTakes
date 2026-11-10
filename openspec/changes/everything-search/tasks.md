## 1. The index

- [x] 1.1 `src/search`: Tantivy behind a C interface, with the forgiving query.
      Rust tests.
- [x] 1.2 Corrosion in `tools/cmake/Search.cmake`, linked into both targets;
      `SearchIndex` (C++) over the C interface. Tests through the real index.

## 2. What is searched

- [x] 2.1 `ScoreSearch`: a score's lyric phrases and bars. Tests on the sample.
- [x] 2.2 `SearchCatalogue`: settings from the catalogue, tools, commands,
      songs, takes, lyrics, bars; `Localisation::keysStartingWith`. Tests.

## 3. The search bar

- [x] 3.1 The magnifying glass, Ctrl+K and Ctrl+F, the popup, and the index
      rebuilt when it opens.
- [x] 3.2 Choosing a result: each kind's action; `SettingsWindow::reveal`.
- [x] 3.3 `search-settings` and `search-lyrics` capture states; captures read
      in both themes; Return on a setting and on a lyric line checked on a
      virtual display.

## 4. Build and CI

- [x] 4.1 `.github/actions/rust-toolchain` in every job that configures CMake;
      curl in the Debian container; the Linux dependency check warns about
      Rust.

## 5. Docs

- [x] 5.1 The wiki (a Search page, Getting Started), BUILDING.md's
      requirements, ARCHITECTURE.md § Search, the agent guide's commands.
