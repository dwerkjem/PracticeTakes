## Context

The maintainer named Tantivy, a Rust full-text search library. Practice Takes
is C++ (JUCE, CMake) with a TypeScript worker. Nothing in it is Rust yet.

## Decisions

### 1. A Rust crate behind a C interface, built by Corrosion

`src/search` is a crate with `crate-type = ["staticlib"]` (plus `rlib`, for its
own tests). It exports a dozen `extern "C"` functions: create and destroy;
clear a kind, add a document, commit; query, then read each hit's fields; and
free the results.

- **Plain C, no JSON:** strings in, an opaque result set out. The C++ side
  needs no JSON parser, and the boundary is small enough to read in one
  screen (`SearchIndexFfi.h`).
- **Nothing unwinds across it:** every entry point catches a panic and
  reports failure.
- **Corrosion** (pinned to v0.6.1 by commit, like JUCE) drives Cargo from
  CMake, for the target CMake configured. It knows which system libraries
  Rust's standard library needs on each platform, which a hand-written custom
  command would have to restate per platform.
- **Always the release profile:** Tantivy unoptimised is many times slower,
  and the crate is not edited alongside the C++.
- **`--locked`:** Cargo.lock decides the crates, as a pinned commit decides
  JUCE.
- **Tantivy 0.26.2 with no default features.** The index is in memory and
  small, so mmap, compression and stemming would be code carried for nothing.

`src/search` sits at the top of `src/`, beside `src/services`, because it is a
component in another language with its own build file, not a layer of the C++.

### 2. In memory, rebuilt from the application's state

The index holds a few thousand short documents at most. Building it costs
milliseconds, and its source is always the application's current state: the
catalogue in the interface language, the open score, the library. An on-disk
index would be a second copy of all of that, with a version, a location and a
way to go stale. So `Index::create_in_ram`, rebuilt as below.

### 3. Documents by kind, replaced whole

Every document has six fields:

- `kind`, which the shell acts on;
- `id`, what the result names;
- `title`, shown and searched first;
- `detail`, shown under the title and searched;
- `keywords`, searched, not shown;
- `body`, searched last.

A kind's documents are replaced together (`SearchIndex::replace`): delete the
kind's term, add, commit. A kind is never edited document by document, so
nothing can be left behind.

**When each kind is rebuilt:** at the moment the search opens, from that
moment's state. That covers settings, tools, commands (whose wording changes
with the state, such as Mute and Unmute) and workspaces. The open score's lines
and bars are rebuilt only when the score itself changed. Songs and takes are
listed on the library's worker and replaced when they arrive, after which the
popup searches again. The reader sees those results a moment later rather than
waiting for them.

### 4. Matching that forgives typing

The query is built in Rust from the reader's words, not parsed by Tantivy's
query language: no operator, quote or `C#` can make a search fail. The text is
split into words by the same analyser the index uses: words, lower-cased,
folded to ASCII.

- **Every word must match**, so each word narrows the list.
- **A word matches a field in three ways:**
  - exactly, at twice the field's weight;
  - as the start of a word, so the last word may be half-typed;
  - one letter out, for words of four letters or more, at 0.4 of the weight.
- **Field weights:** title 4, keywords 2.5, detail 1.5, body 1.

So "refrence" finds Reference pitch, "metr" finds the metronome, and "key"
does not find "kef".

### 5. The score: phrases and bars, as data

`ScoreSearch.h` (JUCE-free) turns the open score into:

- **lyric phrases:** each part's verse-1 syllables, from one breath (a phrase
  start in `PartTimeline`) to the next, joined into words with the hyphens
  removed. "blau- en Au- gen" is found as "blauen Augen", whichever bars it
  spans.
- **bars:** by their printed numbers.

Each carries the tick to move the clock to. The application puts them into
sentences ("Bar 2 · Voice") with the catalogue.

### 6. The popup, an overlay rather than a window

The search bar is a child of the main window, top right, under the magnifying
glass:

- It lists twelve results. The arrow keys move the selection; Return or a
  click chooses.
- **It closes** on Escape, on a choice, or on a press anywhere outside it,
  seen through a desktop mouse listener. It does not close when focus moves,
  because a window taking focus when it activates looks the same as the reader
  going elsewhere, and that made the popup vanish in captures.
- **An overlay, not a call-out window:** the capture harness photographs it
  with the main window, and it never strays from the window it searches.

### 7. Settings from the catalogue, not from the window

Settings' controls are read from the catalogue's keys, not by building the
window and walking it. A key directly under a panel (`settings.<panel>.<name>`)
is a control's label, except for:

- the panel's title, description and heading;
- text longer than a label (60 characters);
- messages with a placeholder.

The control's tooltip, title or question is its body, and its sub-keys'
words (`.flats`, `.sharps`) are its keywords.

- **Why the catalogue:** it is in the interface language, needs no window,
  and is tested.
- **The cost:** a control is found by its key's shape. A new control whose key
  breaks the convention is not found, so the catalogue test asserts the ones
  that matter.

Choosing a setting opens its panel and calls `SettingsWindow::reveal`. That
finds the label or button showing the same words, scrolls it into view, and
rings its row in the Score View's target violet, fading over a second and a
half.

### 8. Rust in CI: installed only where missing

The composite action `.github/actions/rust-toolchain`:

- does nothing where cargo is already there (GitHub's Ubuntu, macOS and
  Windows x64 images);
- otherwise installs a minimal stable toolchain with rustup, for the runner's
  own architecture: the Debian 12 container of the Linux package build, and
  Windows ARM.

Every job that configures CMake runs it, under the same condition as its
configure step. The Linux dependency check warns when Rust is missing rather
than failing, because some jobs run it before the action, and configuring
without Rust stops with Corrosion's own error.

### 9. Captures

Two capture states, built on existing ones:

- `search-settings`: "pitch" over the empty window;
- `search-lyrics`: "augen" over the sample.

The magnifying glass is an approved click target.

## Risks / Trade-offs

- **A second language and toolchain** for one feature. It is the maintainer's
  choice (they named Tantivy), and it is contained to one crate behind one
  header. Removing it would mean replacing `SearchIndex.cpp`'s body and
  nothing else.
- **Build time:** a clean build compiles about forty crates, once per build
  directory.
- **Binary size:** about 3 MB.

## Open Questions

None blocking. Searching the wiki's help pages would need them embedded in the
application, which is a separate decision.
