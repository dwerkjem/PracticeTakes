# Releasing Practice Takes

Practice Takes uses stable semantic versions in the form `MAJOR.MINOR.PATCH`.
Release tags add a leading `v`, such as `v0.2.0` or `v1.0.0`.

## Choosing the next version

- **PATCH**: Increase the last number for a compatible bug fix.
  Example: `0.2.0` to `0.2.1`.
- **MINOR**: Increase the middle number for a meaningful new feature that does
  not intentionally break existing behavior. Reset PATCH to zero.
  Example: `0.2.1` to `0.3.0`.
- **MAJOR**: Increase the first number for deliberately incompatible changes,
  major redesigns, or a new compatibility baseline. Reset MINOR and PATCH to
  zero. Example: `1.4.2` to `2.0.0`.

Versions beginning with `0` indicate active early development. Use `1.0.0` when
Practice Takes has a stable feature set and you are ready to treat compatibility
as a promise to users.

## Publishing a release

First merge the intended changes into `main` and confirm that the multiplatform
build workflow passes. Then create and push an annotated version tag:

```bash
git switch main
git pull --ff-only
git status --short
git tag -a v0.2.0 -m "Practice Takes v0.2.0"
git push origin v0.2.0
```

Replace `v0.2.0` with the version you selected. The release workflow will:

1. Validate the tag format.
2. Build Windows, Linux, and macOS packages for x64 and ARM64.
3. Generate SHA-256 checksums.
4. Create a GitHub Release with automatically generated release notes.
5. Attach all six packages and the checksum file.

Do not reuse or move a published version tag. Make corrections in a new PATCH
release instead.
