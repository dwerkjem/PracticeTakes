## Context

The repository currently has five unique GitHub Actions references across its
workflows, all pinned by mutable tag. It also has one npm workspace
(`services/feedback-intake`) with no automated update coverage. There are no
CMake package manager dependencies beyond manually-pinned `FetchContent` tags.

## Goals / Non-Goals

**Goals:**
- Ensure known vulnerabilities in Actions or npm packages surface as PRs
  automatically.
- Prevent silent execution of a tag-moved malicious commit in CI.

**Non-Goals:**
- No Renovate (Dependabot is already built into GitHub; adding both would
  produce duplicate PRs).
- No CMake/`FetchContent` coverage (Dependabot does not support it; the
  existing JUCE/Catch2 pins are already immutable git tags).
- No change to what the workflows actually do — only how they reference
  their Actions.

## Decisions

**1. Dependabot covers `github-actions` and `npm` only, weekly cadence.**
Weekly is the right trade-off for a single-maintainer project: daily would
generate noise, monthly is too slow for security fixes. The `npm` entry is
scoped to `services/feedback-intake/` (the only npm workspace with
dependencies) so it generates targeted PRs rather than workspace-root noise.

**2. SHA-pinning uses full 40-character commit SHAs with version comment.**
The commit SHA is resolved from the tag's annotated-tag object at the time of
this change (the tag SHA must be dereferenced to the underlying commit SHA,
not the tag object SHA, to be accepted by GitHub Actions). The version comment
`# v6` / `# v4` etc. keeps diffs and reviews human-readable. Once Dependabot
is active, it will keep these SHAs current automatically.

**3. SHA-pinning is applied to all five Actions references in one pass.**
Doing them individually across multiple PRs would leave a partially-pinned
state. One atomic change is cleaner.

**4. The reusable workflow call (`./.github/workflows/build-multiplatform.yml`)
is not SHA-pinned — it is a local relative path, not an external action.**
Local reusable workflow calls use the same ref as the calling workflow by
definition; there is nothing to pin.

## Resolved SHAs (at time of change)

| Action                    | Tag | Commit SHA                               |
|---------------------------|-----|------------------------------------------|
| actions/checkout          | v6  | d23441a48e516b6c34aea4fa41551a30e30af803 |
| actions/cache             | v4  | 0057852bfaa89a56745cba8c7296529d2fc39830 |
| actions/setup-node        | v4  | 49933ea5288caeca8642d1e84afbd3f7d6820020 |
| actions/upload-artifact   | v4  | ea165f8d65b6e75b540449e92b4886f43607fa02 |
| actions/download-artifact | v5  | 634f93cb2916e3fdff6788551b99b062d0335ce0 |

## Risks / Trade-offs

- **[Trade-off]** SHA-pinned workflows are slightly harder to read at a
  glance. Mitigated by the inline `# vN` comment on every pinned reference.
- **[Risk]** Dependabot will open PRs for minor/patch npm bumps that may
  include breaking changes if `feedback-intake`'s `package.json` uses
  non-exact version ranges. Accepted: PRs go through CI before merging;
  breaking changes will fail `npm run check`/`npm run test` and block merge.
