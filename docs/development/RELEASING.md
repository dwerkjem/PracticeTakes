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

The workflow calculates the next version, commits the version files, and
creates the version tag before calling the shared multiplatform build workflow.
The tag makes the release source immutable, so later changes to `main` cannot
change or invalidate a build already in progress. The build workflow checks out
that exact release commit, builds Windows, Linux, and macOS packages for x64 and
ARM64, uploads each package as an artifact, then assembles one verified
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

After creating the immutable release commit and tag, the workflow builds and
verifies all six packages, then publishes the release. If a build or publishing
step fails, use **Re-run failed jobs** on the same workflow run so it continues
to use the same tagged commit. The same artifact path is used for patch, minor,
and major releases.

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

## Artifact authenticity

Every published asset carries GitHub build provenance, which records the
workflow, commit, and runner that produced it. Anyone can verify a download
without any certificate on our side:

```bash
gh attestation verify PracticeTakes-VERSION-linux-x64.deb --repo dwerkjem/PracticeTakes
```

Provenance proves where a file came from. It does not stop the operating
system from warning about an unrecognised publisher, because that requires a
platform code-signing certificate. The release workflow already contains the
Windows and macOS signing steps, gated on repository secrets that do not exist
yet. While those secrets are absent every signing step skips and the release
behaves exactly as it does today, so the sections below can be completed one
platform at a time.

### Windows Authenticode

Buy an OV or EV code-signing certificate from a certificate authority. EV
certificates clear the SmartScreen reputation prompt immediately; OV
certificates build reputation over time. The authority issues a PKCS#12 file.

Convert it to the single-line base64 the workflow expects:

```bash
base64 -w0 practice-takes.pfx > practice-takes.pfx.b64   # Linux
base64 -b 0 -i practice-takes.pfx -o practice-takes.pfx.b64   # macOS
```

Create two repository secrets under **Settings → Secrets and variables →
Actions**:

- `WINDOWS_CERT_PFX_BASE64` — the contents of that `.b64` file
- `WINDOWS_CERT_PASSWORD` — the password protecting the PKCS#12

Signing activates as soon as both are present. The workflow locates
`signtool.exe` in the runner's Windows SDK, signs `build/bin/PracticeTakes.exe`
before CPack collects it so the installer carries a signed binary, then signs
the installer itself and runs `signtool verify /pa` on the result. Delete the
local `.pfx` and `.b64` files once the secrets are stored.

Owners using Azure Trusted Signing can replace the `signtool` steps with
`azure/trusted-signing-action` and keep the same `WINDOWS_SIGNING_ENABLED`
gate.

### macOS Developer ID and notarization

Enrol in the Apple Developer Program, then create both a **Developer ID
Application** and a **Developer ID Installer** certificate. The first signs the
`.app` bundle and the second signs the `.pkg` installer; a release needs both.

Export the two identities from Keychain Access into one `.p12` file, then
base64-encode it as above. Read the exact identity names, which include the
team identifier in parentheses:

```bash
security find-identity -v -p codesigning
```

Create four secrets to enable signing:

- `APPLE_DEVELOPER_ID_CERT_P12` — base64 of the combined `.p12`
- `APPLE_DEVELOPER_ID_CERT_PASSWORD` — its export password
- `APPLE_DEVELOPER_ID_APPLICATION` — `Developer ID Application: Name (TEAM)`
- `APPLE_DEVELOPER_ID_INSTALLER` — `Developer ID Installer: Name (TEAM)`

Add three more to enable notarization, which macOS requires before Gatekeeper
will open a downloaded package without a warning:

- `APPLE_NOTARY_APPLE_ID` — the Apple ID owning the Developer ID
- `APPLE_NOTARY_PASSWORD` — an app-specific password for that Apple ID,
  generated at [appleid.apple.com](https://appleid.apple.com), not the account
  password
- `APPLE_TEAM_ID` — the ten-character team identifier

Sign both `.app` and `.pkg`, and notarize, or the result is worse than shipping
unsigned: a Developer ID signature without notarization is actively rejected on
current macOS. Notarization runs `notarytool submit --wait`, then staples the
ticket to the package so it validates offline. Stapling rewrites the package,
so it happens before the checksums and provenance are taken.

With the signing secrets absent the workflow keeps its ad-hoc `codesign --sign
-` signature and the build is byte-identical to today's.

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
python3 tools/scripts/release/version.py current
python3 tools/scripts/release/version.py next patch
python3 tools/scripts/release/version.py next minor
python3 tools/scripts/release/version.py next major
```

To update the local `VERSION` file and synchronize `vcpkg.json`:

```bash
python3 tools/scripts/release/version.py bump patch
python3 tools/scripts/release/version.py bump minor
python3 tools/scripts/release/version.py bump major
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
