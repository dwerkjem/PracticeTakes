# Releasing Practice Takes

Practice Takes has one source of truth for its version: the root
[`VERSION`](../../VERSION) file. CMake, JUCE application metadata, the C++
application version, window titles, package names, tags, build artifacts, and
GitHub Releases all derive from that value. When the version helper writes a
release version, it also synchronizes the `version-string` in `vcpkg.json`.

Do not copy the version into `CMakeLists.txt` or C++ source files.

## Automated release

1. Merge the intended changes into `main`.
2. Open the repository's **Actions** tab.
3. Select **Create Practice Takes release**.
4. Select **Run workflow**.
5. Choose `patch`, `minor`, or `major`.
6. Select **Run workflow** again.

The workflow calculates the next version and calls the shared multiplatform
build workflow. That workflow builds Windows, Linux, and macOS packages for x64
and ARM64, uploads each package as an artifact, then assembles one verified
release-artifact bundle.

Release and Clang-Tidy auto-fix runs share a FIFO queue. If Clang-Tidy is
already running or waiting after a relevant push to `main`, a newly requested
release waits for it to finish before selecting the source commit and starting
the package builds. Queued runs are not canceled when another run joins the
queue.

The release workflow does not rebuild those packages during publishing. It
downloads the bundle produced by the build workflow, verifies the version,
source commit, expected six package files, and SHA-256 checksums, then publishes
those exact files to the GitHub Release.

After all six builds and artifact verification pass, the workflow changes
`VERSION`, synchronizes the vcpkg manifest, commits both changes, creates the
version tag, and publishes the release. The same artifact path is used for
patch, minor, and major releases.

## Release artifact bundle

The shared build workflow produces these release packages:

- `PracticeTakes-VERSION-linux-x64.deb`
- `PracticeTakes-VERSION-linux-arm64.deb`
- `PracticeTakes-VERSION-windows-x64.exe`
- `PracticeTakes-VERSION-windows-arm64.exe`
- `PracticeTakes-VERSION-macos-x64.pkg`
- `PracticeTakes-VERSION-macos-arm64.pkg`

The Linux packages declare their runtime dependencies for APT and install a
desktop Applications-menu entry. The Windows installers bundle the compiler
runtime and create a Start Menu shortcut. The macOS packages install the
application bundle in `/Applications`.

It also adds:

- `SHA256SUMS.txt` for package integrity checks
- `BUILD-METADATA.txt` containing the version, source commit, and originating
  workflow run

Individual platform artifacts are retained for 14 days. The combined
release-artifact bundle is retained for 30 days, so a failed publishing step can be
diagnosed or recovered without immediately rebuilding every platform.

## Choosing a release type

Practice Takes uses semantic versions in the form `MAJOR.MINOR.PATCH`. Release
tags add a leading `v`, such as `v0.2.0` or `v1.0.0`.

- **PATCH** increases the last number for a compatible bug fix.
  Example: `0.2.0` to `0.2.1`.
- **MINOR** increases the middle number for compatible functionality and resets
  PATCH to zero. Example: `0.2.1` to `0.3.0`.
- **MAJOR** increases the first number for deliberately incompatible changes,
  a major redesign, or a new compatibility baseline. MINOR and PATCH reset to
  zero. Example: `1.4.2` to `2.0.0`.

Versions beginning with `0` indicate active early development. Use `1.0.0`
when the feature set and compatibility expectations are stable enough to be a
public promise.

## Local version commands

The same version calculation is available locally:

```bash
python3 scripts/release/version.py current
python3 scripts/release/version.py next patch
python3 scripts/release/version.py next minor
python3 scripts/release/version.py next major
```

To update the local `VERSION` file and synchronize `vcpkg.json`:

```bash
python3 scripts/release/version.py bump patch
python3 scripts/release/version.py bump minor
python3 scripts/release/version.py bump major
```

The automated workflow is preferred for published releases because it builds
every supported platform, verifies the complete artifact bundle, and publishes
only the files that passed the shared build process.

## Manual tag compatibility

A manually pushed tag still works when it exactly matches the committed
`VERSION` value:

```bash
git switch main
git pull --ff-only
git tag -a v0.2.0 -m "Practice Takes v0.2.0"
git push origin v0.2.0
```

The release workflow then calls the shared build workflow, assembles and
verifies the release-artifact bundle, generates release notes, and publishes
the release. Never reuse or move a published version tag; make corrections in
a new PATCH release.
