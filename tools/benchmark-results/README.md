# Benchmark Results

The development-only Performance Lab writes immutable JSON evidence into this directory after
each run. Run IDs are generated automatically from:

- UTC timestamp;
- detected operating system;
- strategy identifier;
- sample rate and buffer size;
- source commit; and
- a short UUID suffix to prevent collisions.

Review results against `docs/development/performance/hardware-acceptance.md` before committing
them. Synthetic, interrupted, or uncontrolled runs should remain identifiable through their
status, warnings, and provenance rather than being edited after capture.