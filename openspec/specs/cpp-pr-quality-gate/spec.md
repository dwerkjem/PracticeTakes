## ADDED Requirements

### Requirement: Pull requests are checked for C++ formatting
Every pull request that modifies a `.c`, `.cc`, `.cpp`, `.cxx`, `.h`, `.hh`,
`.hpp`, or `.hxx` file under `src/` SHALL run `clang-format` in check mode
(no in-place modification) against every such file in the repository, and
the check SHALL fail if any file differs from its formatted output.

#### Scenario: Formatted PR passes the check
- **WHEN** a pull request's C++ files all match `.clang-format`'s expected
  output
- **THEN** the formatting check succeeds

#### Scenario: Unformatted PR fails the check
- **WHEN** a pull request introduces or leaves a C++ file that does not
  match `.clang-format`'s expected output
- **THEN** the formatting check fails and identifies the non-conforming
  file(s)

### Requirement: Pull requests are checked for clang-tidy findings
Every pull request that modifies a `.cpp` file under `src/` SHALL run
`clang-tidy` (without `--fix`) against every `.cpp` file discovered
recursively under `src/`, using the project's `.clang-tidy` configuration,
and the check SHALL fail if any finding is reported in a check category
listed under `WarningsAsErrors`.

#### Scenario: Clean PR passes the analysis check
- **WHEN** a pull request's changes produce no clang-tidy findings in a
  `WarningsAsErrors` category across the full recursive `src/` file set
- **THEN** the analysis check succeeds

#### Scenario: New finding fails the analysis check
- **WHEN** a pull request introduces code that triggers a clang-tidy finding
  in a `WarningsAsErrors` category
- **THEN** the analysis check fails and reports the finding's file, line, and
  check name

#### Scenario: Recursive discovery matches the post-merge job
- **WHEN** the PR-time analysis check and the post-merge `clang-tidy-main.yml`
  job both run against the same commit
- **THEN** both discover the same set of `.cpp` files under `src/`
  (recursively, not only files directly inside `src/`)

### Requirement: Post-merge analysis discovers all source files recursively
The post-merge clang-tidy workflow SHALL discover every `.cpp` and `.h` file
anywhere under `src/`, at any directory depth, rather than only files
directly inside the top-level `src/` directory.

#### Scenario: Nested source file is analyzed
- **WHEN** a `.cpp` file exists at any depth under `src/` (for example
  `src/features/analysis/tuner/PitchDetector.cpp`)
- **THEN** the post-merge clang-tidy workflow includes that file in its
  analysis and automatic-fix pass

#### Scenario: No silent zero-file success
- **WHEN** the post-merge clang-tidy workflow runs
- **THEN** it fails loudly (non-zero exit, visible error) if file discovery
  resolves to zero files, rather than reporting success
