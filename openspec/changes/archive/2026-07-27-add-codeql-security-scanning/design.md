## Context

Practice Takes is a mixed-language repository with C++ application code and a TypeScript services workspace. Existing CI covers compilation, tests, formatting, clang-tidy checks, and benchmark reporting. Security scanning is not yet codified as a standard CI signal.

## Goals

- Add repeatable CodeQL scanning for all supported languages in this repository.
- Keep workflow behavior predictable and aligned with existing CI conventions.
- Ensure action references remain SHA pinned.

## Non-goals

- No custom query packs in this change.
- No hard fail policy tuning beyond CodeQL defaults.
- No SARIF post-processing or external dashboards.

## Decisions

1. Create a dedicated `codeql.yml` workflow.
   - Keeps security scanning separate from build/package flows.

2. Use a matrix over languages (`cpp`, `javascript-typescript`).
   - Avoids duplicate workflow files and keeps visibility unified.

3. Build only for `cpp` jobs.
   - C++ analysis quality depends on compilation context.
   - JS/TS analysis does not require native compilation.

4. Trigger on `push` and `pull_request` to `main`, plus weekly schedule.
   - PR coverage catches newly introduced issues.
   - Weekly run catches environment/query improvements over time.

5. Use pinned SHAs for `checkout`, `codeql-action/init`, `codeql-action/autobuild`, and `codeql-action/analyze`.
   - Matches repository supply-chain hygiene requirements.

## Risks

- C++ build failures would prevent completion of the C++ CodeQL job.
  - Mitigation: use established Linux dependency setup script and explicit CMake configure/build steps.
- Query execution time may increase CI duration.
  - Mitigation: scoped triggers (`main`, PR to `main`, weekly) and matrix isolation.
