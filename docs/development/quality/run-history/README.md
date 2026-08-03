# Run history

One JSON file per verification run: what passed, what failed, what the automated
suites said, and what was measured — written by `uv run test-suite sync` and
read by the hub's History view.

**This is how history leaves a machine.** The testing suite keeps a local SQLite
store of everything, but a binary database cannot be shared through git: two
machines committing different runs would conflict on every byte. One text file
per run merges without conflict, so pulling the repository is all it takes for
another machine's runs to appear on the graphs.

**Images are deliberately not here.** A full run is several megabytes of PNG that
would bloat the repository forever and cannot be diffed. Screenshots stay in the
local store — `~/.local/share/practice-takes-testing-suite/images/` — and only
the numbers are shared. `uv run test-suite prune` clears old images without
touching anything recorded here.

Each file carries the machine it ran on, and a comparison never crosses
machines: a launch time from another processor is not a point on this machine's
line.

These are **not** the release gate's input. That remains the run records in
`../manual-runs/`, read by `tools/scripts/release/check_manual_verification.py`.
