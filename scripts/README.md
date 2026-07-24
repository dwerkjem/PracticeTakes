# Project scripts

Scripts are grouped by their purpose:

- `build/` configures, builds, and runs the desktop application.
- `feedback/` manages the feedback database and dashboard service.
- `quality/` runs source-formatting and static-analysis checks.
- `release/` manages application versions.
- `roadmap/` sets up the roadmap tooling.
- `practice_takes_roadmap/` contains the roadmap Python package.

Run scripts from the repository root unless a script says otherwise. For
example:

```bash
./scripts/build/build-and-run.sh
./scripts/feedback/migrate-feedback-database.sh --remote
./scripts/feedback/run-recovery-drill.py
```
