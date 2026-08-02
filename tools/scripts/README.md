# Project scripts

Scripts are grouped by their purpose:

- `build/` configures, builds, and runs the desktop application.
- `design/` regenerates committed design assets such as the application icon.
- `feedback/` manages the feedback database and dashboard service.
- `quality/` runs source-formatting and static-analysis checks.
- `release/` manages application versions.
- `roadmap/` sets up the roadmap tooling.
- `secrets/` encrypts, synchronizes, and resolves configured SOPS secrets.
- `practice_takes_roadmap/` contains the roadmap Python package.

Run scripts from the repository root unless a script says otherwise. For
example:

```bash
./tools/scripts/build/build-and-run.sh
./tools/scripts/feedback/migrate-feedback-database.sh --remote
./tools/scripts/feedback/configure-cloudflare-access.sh
./tools/scripts/feedback/run-recovery-drill.py
./tools/scripts/quality/run-performance-lab.sh
./tools/scripts/quality/ui-validation/run-ui-golden.zsh
```

The Performance Lab script starts the application, calibrates instrumentation,
runs the baseline and parameterized strategies, saves each immutable result in
the normal local and repository result stores, and exits without user input.
Configure the build with `-DPRACTICE_TAKES_ENABLE_PERFORMANCE_LAB=ON` before
running it.

The UI golden script uses isolated profiles to capture settled seven-second
first-launch and representative restored-workspace references, then times fresh
launches until their process-owned X11 content matches exactly. It excludes the
native title bar, mutes the microphone for the run, verifies a maximize-and-
restore repaint, and captures settings-import and workspace-recovery workflows
at desktop and constrained sizes. It parks and restores the pointer and checks
that alternate off-window pointer positions do not affect workflow pixels.
Generated helpers, images, and the evidence manifest are written to
`build/ui-validation/step-7/`.
