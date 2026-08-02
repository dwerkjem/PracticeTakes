# Security policy

## Reporting a vulnerability

Practice Takes uses GitHub's private security advisory feature for
vulnerability reports. Do not open a public issue for a security
vulnerability.

To report a vulnerability:

1. Go to the [Security tab](https://github.com/dwerkjem/PracticeTakes/security) of this repository.
2. Select **Report a vulnerability**.
3. Describe the issue, including steps to reproduce and any relevant
   version information.

This opens a private advisory visible only to the maintainer and the
reporter, so the issue can be discussed and fixed before public disclosure.

## Supported versions

Practice Takes is early-stage software distributed as a single current
release (see the root `VERSION` file). Only the latest released version is
supported.

An `LTS` branch exists at `v0.5.6` as groundwork for a future long-term-support
line. It is not an active support line yet: nothing is backported to it and no
LTS release has been made. Until that changes, treat the latest release as the
only supported version.

Support here means source-level fixes. The hosted feedback service applies its
own minimum supported version and rejects submissions from clients older than
that floor, so an unsupported build cannot send feedback even though it still
runs locally.
