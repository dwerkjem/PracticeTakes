# Project scripts

Scripts are grouped by their purpose:

- `build/` configures, builds, and runs the desktop application.
- `ci/` is what workflows call rather than inline — currently the reporter that
  files an unattended workflow failure into the issue queue.
- `design/` regenerates committed design assets such as the application icon.
- `feedback/` manages the feedback database and dashboard service.
- `quality/` runs source-formatting and static-analysis checks.
- `release/` manages application versions.
- `secrets/` encrypts, synchronizes, and resolves configured SOPS secrets.

Run scripts from the repository root unless a script says otherwise. For
example:

```bash
./tools/scripts/build/build-and-run.sh
./tools/scripts/feedback/migrate-feedback-database.sh --remote
./tools/scripts/feedback/configure-cloudflare-access.sh
./tools/scripts/feedback/run-recovery-drill.py
./tools/scripts/quality/ui-validation/run-ui-golden.zsh
```

The UI golden script uses isolated profiles to capture settled seven-second
first-launch and representative restored-workspace references, then times fresh
launches until their process-owned X11 content matches exactly. It excludes the
native title bar, mutes the microphone for the run, verifies a maximize-and-
restore repaint, and captures settings-import and workspace-recovery workflows
at desktop and constrained sizes. It parks and restores the pointer and checks
that alternate off-window pointer positions do not affect workflow pixels.
Generated helpers, images, and the evidence manifest are written to
`build/ui-validation/step-7/`.
