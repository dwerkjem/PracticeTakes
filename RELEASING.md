# Releasing Practice Takes

Practice Takes has one persistent version value: the root [`VERSION`](VERSION)
file. CMake, JUCE application metadata, the C++ `getApplicationVersion()` value,
window titles, package names, tags, and GitHub Releases all derive from that
value.

Do not copy the version into `CMakeLists.txt` or `src/main.cpp`.

## Easiest release method

1. Merge the intended changes into `main`.
2. Open the repository's **Actions** tab.
3. Select **Create Practice Takes release**.
4. Select **Run workflow**.
5. Choose one release type:
   - `patch`
   - `minor`
   - `major`
6. Select **Run workflow** again.

The workflow calculates the next version and builds Windows, Linux, and macOS
packages for x64 and ARM64. It changes `VERSION`, commits the change, creates the
version tag, and publishes the GitHub Release only after all six builds pass.

## Choosing the release type

Practice Takes uses stable semantic versions in the form `MAJOR.MINOR.PATCH`.
Release tags add a leading `v`, such as `v0.2.0` or `v1.0.0`.

- **PATCH**: Increase the last number for a compatible bug fix.
  Example: `0.2.0` to `0.2.1`.
- **MINOR**: Increase the middle number for a meaningful compatible feature.
  PATCH resets to zero. Example: `0.2.1` to `0.3.0`.
- **MAJOR**: Increase the first number for deliberately incompatible changes,
  a major redesign, or a new compatibility baseline. MINOR and PATCH reset to
  zero. Example: `1.4.2` to `2.0.0`.

Versions beginning with `0` indicate active early development. Use `1.0.0` when
Practice Takes has a stable feature set and compatibility is ready to be treated
as a promise to users.

## Local version commands

The same version logic is available locally:

```bash
python3 scripts/version.py current
python3 scripts/version.py next patch
python3 scripts/version.py next minor
python3 scripts/version.py next major
```

To update only your local `VERSION` file:

```bash
python3 scripts/version.py bump patch
python3 scripts/version.py bump minor
python3 scripts/version.py bump major
```

The automated Actions workflow is preferred for published releases because it
builds every supported platform before committing the version and creating the
tag.

## Manual tag compatibility

A manually pushed tag still works, provided it exactly matches the committed
`VERSION` value:

```bash
git switch main
git pull --ff-only
git tag -a v0.2.0 -m "Practice Takes v0.2.0"
git push origin v0.2.0
```

The release workflow then builds all six packages, creates SHA-256 checksums,
generates release notes, and publishes the release. Never reuse or move a
published version tag; make corrections in a new PATCH release.
