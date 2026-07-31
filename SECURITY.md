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

Practice Takes is early-stage software. Two lines receive support:

| Line | Ref | What it receives |
| --- | --- | --- |
| Current | `main`, latest tag | Features and fixes, including security fixes |
| Long-term support | `LTS` branch | Security fixes only |

The current release is recorded in the root `VERSION` file. The `LTS` branch
starts at `v0.5.6` and moves forward only when a security fix is backported to
it. Releases older than the `LTS` branch point are not supported, and no fixes
are issued for them.

Support here means source-level fixes. The hosted feedback service applies its
own minimum supported version and rejects submissions from clients older than
that floor, so an unsupported build cannot send feedback even though it still
runs locally.
