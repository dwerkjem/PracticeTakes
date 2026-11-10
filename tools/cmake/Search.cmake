# The search index (openspec/changes/everything-search): the Rust crate in
# src/search, which wraps Tantivy behind a C interface, built by Cargo and
# linked into both targets as the `practice_takes_search` static library.
#
# Corrosion drives Cargo from CMake: it builds the crate for the target CMake
# configured, and names the system libraries Rust's standard library needs on
# each platform, which a hand-written custom command would have to restate.
#
# A Rust toolchain (cargo and rustc, 1.86 or newer) is therefore a build
# requirement on every platform, as a C++ compiler is; GitHub's runners ship
# one. The crate's dependencies are locked by src/search/Cargo.lock and fetched
# from crates.io at build time, as JUCE is fetched from GitHub.
FetchContent_Declare(
    Corrosion
    GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
    GIT_TAG 1499b14e4906a2890f5cee1547c8848db261753d # v0.6.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Corrosion)

# Always the release profile: Tantivy unoptimised is many times slower, and the
# crate is built once per configuration, not edited alongside the C++. LOCKED,
# so a build never quietly takes newer crates than Cargo.lock names.
corrosion_import_crate(
    MANIFEST_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/search/Cargo.toml"
    CRATES practice_takes_search
    CRATE_TYPES staticlib
    PROFILE release
    LOCKED
)
