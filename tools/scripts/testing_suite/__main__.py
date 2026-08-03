#!/usr/bin/env python3
"""The testing suite's command line.

    test-suite                    # the hub: pick what to run, run it, review it

Everything the hub does has a subcommand too, for scripting and for a machine
with no browser:

    test-suite run --suites cpp python      # or --all, --kind performance
    test-suite capture
    test-suite attend
    test-suite review
    test-suite ingest --performance lab-export.json
    test-suite export
    test-suite prune --keep 5

The hub is the default because remembering five commands, three build
directories, and which of them needs a display is exactly the friction this
exists to remove. It builds what a suite needs before running it.

No CI check runs any of this.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import platform
import subprocess
import sys
import threading
import webbrowser

sys.path.insert(0, str(Path(__file__).resolve().parent))

import attend as attend_module  # noqa: E402
import capture as capture_module  # noqa: E402
import runner as runner_module  # noqa: E402
import suites as suites_module  # noqa: E402
import export as export_module  # noqa: E402
import history as history_module  # noqa: E402
import ingest as ingest_module  # noqa: E402
import machine as machine_module  # noqa: E402
import review as review_module  # noqa: E402
import server as server_module  # noqa: E402
import surfaces  # noqa: E402
from driver import ApplicationDriver, ChannelError, missing_states  # noqa: E402
from store import Store, StoreError  # noqa: E402

REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_EXECUTABLE = REPOSITORY_ROOT / "build-tc" / "bin" / "PracticeTakes"


def current_commit() -> str:
    try:
        return subprocess.run(
            ["git", "rev-parse", "--short=12", "HEAD"],
            cwd=REPOSITORY_ROOT,
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return "unknown"


def describe_platform() -> str:
    return f"{platform.system()} {platform.release()} ({platform.machine()})"


def open_store(arguments) -> Store:
    return Store.open(arguments.database)


def resolve_run(store: Store, arguments) -> int:
    if getattr(arguments, "run", None):
        return int(arguments.run)

    latest = store.latest_run()

    if latest is None:
        raise SystemExit("No runs in the store yet. Start with `test-suite capture`.")

    return int(latest["id"])


def image_directory(store: Store, run_id: int) -> Path:
    return store.path.parent / "images" / f"run-{run_id}"


# --- Commands ---------------------------------------------------------------


def command_capture(arguments) -> int:
    problems = surfaces.validate()

    if problems:
        print("The surface list is malformed:\n  " + "\n  ".join(problems), file=sys.stderr)

        return 1

    resolutions = tuple(arguments.resolutions)
    plan = surfaces.plan(arguments.mode, resolutions)
    store = open_store(arguments)

    try:
        tooling = capture_module.Tooling.ensure()
    except capture_module.CaptureError as error:
        print(str(error), file=sys.stderr)

        return 1

    driver = ApplicationDriver(arguments.executable)

    try:
        driver.start()
    except ChannelError as error:
        print(str(error), file=sys.stderr)

        return 1

    try:
        absent = missing_states(driver.list_states(), surfaces.required_states())

        if absent:
            print(
                "The application does not offer these states the surface list needs:\n  "
                + "\n  ".join(absent),
                file=sys.stderr,
            )

            return 1

        # Resuming names an existing run; asking the store for it first turns a
        # mistyped id into a message rather than a foreign-key traceback.
        run_id = int(store.run(int(arguments.run))["id"]) if arguments.run else store.start_run(
            provenance=machine_module.provenance(),
            commit=current_commit(),
            mode=arguments.mode,
            resolutions=resolutions,
            build_config=arguments.build_config,
            platform=describe_platform(),
            audio_device=arguments.audio_device,
        )

        pass_ = capture_module.CapturePass(
            store=store,
            run_id=run_id,
            driver=driver,
            tooling=tooling,
            image_directory=image_directory(store, run_id),
            window_title=arguments.window_title,
        )
        result = pass_.run(plan)
    except ChannelError as error:
        print(f"The control channel failed: {error}", file=sys.stderr)

        return 1
    finally:
        driver.stop()

    print(
        f"Run {run_id}: {result['captured']} captured, {result['failed']} failed, "
        f"{result['already_captured']} already present."
    )
    print(f"Review it with:  test-suite review --run {run_id}")

    return 0


def command_attend(arguments) -> int:
    store = open_store(arguments)
    run_id = resolve_run(store, arguments)
    pending = review_module.outstanding(store, run_id, attended=True)

    if not pending:
        print(f"Run {run_id} has no outstanding attended questions.")

        return 0

    driver = ApplicationDriver(arguments.executable)

    try:
        driver.start()
    except ChannelError as error:
        print(str(error), file=sys.stderr)

        return 1

    try:
        result = attend_module.AttendedPass(store=store, run_id=run_id, driver=driver).run()
    finally:
        driver.stop()

    print(f"\n{result['answered']} answered, {result['remaining']} still outstanding.")

    return 0


def command_hub(arguments) -> int:
    """Serve the hub. Works against an empty store — that is the normal first visit."""
    store = open_store(arguments)
    run_id = None if getattr(arguments, "run", None) is None else int(arguments.run)
    httpd = server_module.serve(store, run_id, arguments.port)
    address = f"http://{server_module.HOST}:{arguments.port}/"
    latest = store.latest_run()

    print(f"Testing suite at {address}")

    if latest is None:
        print("The store is empty — pick some suites in the Run tab and the hub will")
        print("build whatever they need first.")
    else:
        print(f"Newest run: {latest['id']} ({latest['mode']}, {latest['started_at']})")

    print("Ctrl-C when you are done; everything is saved as you go.")

    if not arguments.no_browser:
        threading.Timer(0.5, lambda: webbrowser.open(address)).start()

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        httpd.server_close()

    return 0


def command_run(arguments) -> int:
    """Run suites from the command line, with the same job the hub uses."""
    store = open_store(arguments)
    chosen = list(arguments.suites or [])

    if arguments.all:
        chosen = [suite.id for suite in suites_module.SUITES]
    elif arguments.kind:
        chosen = [suite.id for suite in suites_module.of_kind(arguments.kind)]

    if not chosen:
        print("Name some suites with --suites, or use --all or --kind.", file=sys.stderr)
        print("Available: " + ", ".join(suite.id for suite in suites_module.SUITES), file=sys.stderr)

        return 2

    job = runner_module.Job(store=store)
    seen = 0

    if not job.start(chosen, mode=arguments.mode, resolutions=tuple(arguments.resolutions),
                     rebuild=arguments.rebuild, run_id=arguments.run):
        print("Nothing to run.", file=sys.stderr)

        return 2

    while True:
        status = job.status()

        for line in status["log"][seen:]:
            print(line)

        seen = len(status["log"])

        if not status["running"]:
            break

        job.wait(0.4)

    status = job.status()
    failed = [entry for entry in status["results"].values() if entry.get("state") == "failed"]

    for entry in status["results"].values():
        print(f"  {entry['id']:<16} {entry.get('state', '?')}")

    return 1 if failed or status["state"] == runner_module.FAILED else 0


def command_review(arguments) -> int:
    store = open_store(arguments)
    run_id = resolve_run(store, arguments)
    httpd = server_module.serve(store, run_id, arguments.port)
    address = f"http://{server_module.HOST}:{arguments.port}/"

    print(f"Reviewing run {run_id} at {address}")
    print("Ctrl-C when you are done; everything is saved as you go.")

    if not arguments.no_browser:
        threading.Timer(0.5, lambda: webbrowser.open(address)).start()

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        httpd.server_close()

    remaining = review_module.outstanding(store, run_id)

    if remaining:
        print(f"{len(remaining)} question(s) still unanswered — the run will export as incomplete.")

    return 0


def command_ingest(arguments) -> int:
    store = open_store(arguments)
    run_id = resolve_run(store, arguments)

    try:
        if arguments.performance:
            count = ingest_module.performance_export(store, run_id, arguments.performance)
            print(f"Ingested {count} measurement(s) into run {run_id}.")
        elif arguments.suite:
            summary = ingest_module.test_result(
                store,
                run_id,
                suite=arguments.suite,
                path=arguments.report,
                cases=arguments.cases,
                failures=arguments.failures,
            )
            print(
                f"Recorded {arguments.suite}: {summary['cases']} case(s), "
                f"{summary['failures']} failure(s)."
            )
        else:
            print("Give --performance or --suite.", file=sys.stderr)

            return 2
    except ingest_module.IngestError as error:
        print(str(error), file=sys.stderr)

        return 1

    return 0


def command_export(arguments) -> int:
    store = open_store(arguments)
    run_id = resolve_run(store, arguments)

    try:
        json_path, markdown_path = export_module.write(store, run_id, arguments.directory)
    except ValueError as error:
        print(f"The run cannot be exported yet:\n  {error}", file=sys.stderr)

        return 1

    run = export_module.build(store, run_id)
    status = "complete" if run.complete else "INCOMPLETE"
    print(f"{status} run exported:\n  {json_path}\n  {markdown_path}")

    if run.unanswered:
        print(f"\n{len(run.unanswered)} question(s) were never answered:")

        for entry in run.unanswered[:10]:
            print(f"  - {entry['surface']} ({entry['geometry']}): {entry['prompt']}")

    failures = review_module.failures(store, run_id)

    if failures:
        print(f"\n{len(failures)} failure(s):")

        for failure in failures:
            detail = failure.get("reason") or f"{failure.get('prompt')} — {failure.get('note')}"
            print(f"  - {failure['surface']} ({failure['geometry']}): {detail}")

    return 0


def command_sync(arguments) -> int:
    """Write this machine's runs into the git-tracked history directory."""
    store = open_store(arguments)
    result = history_module.sync(store, arguments.directory)

    print(f"{len(result['written'])} run(s) written, {result['unchanged']} already current.")
    print(f"{result['total']} run(s) now in {result['directory']}")

    if result["written"]:
        print("\nCommit them to share this history:")
        print(f"  git add {result['directory']} && git commit -m 'chore: record test runs'")

    print("\nImages stay on this machine; only the numbers are shared.")

    return 0


def command_history(arguments) -> int:
    """The same numbers the graphs draw, for a terminal."""
    store = open_store(arguments)
    entries = history_module.collect(store, arguments.directory)

    if not entries:
        print("No runs recorded yet.")

        return 0

    machine = arguments.machine or entries[-1]["machine"]
    data = history_module.series(entries, machine)

    print(f"{len(data['runs'])} scored run(s) on machine {machine[:8]} "
          f"({data['synced']} synced, {data['local_only']} local only)\n")

    for run in data["runs"][-arguments.limit :]:
        print(f"  {run['started_at']}  {run['commit'][:8]}  {run['mode']:<5}  "
              f"{run['pass_percent']:>5}% passed  ({run['failed']} failed)")

    if data["metrics"]:
        print("\nMeasurements (latest, and change from the run before):")

        for metric in data["metrics"]:
            summary = history_module.trend(metric["points"])
            delta = "" if summary.get("delta") is None else f"  {summary['delta']:+g}"
            print(f"  {metric['metric']:<45} {summary['latest']:>10g} {metric['unit']:<3}{delta}")

    return 0


def command_prune(arguments) -> int:
    store = open_store(arguments)
    removed = store.prune_images(arguments.keep)

    print(f"Removed {len(removed)} image file(s); every verdict, tag, and comment is untouched.")

    return 0


def command_status(arguments) -> int:
    store = open_store(arguments)
    runs = store.runs()

    if not runs:
        print("No runs yet.")

        return 0

    for row in runs[: arguments.limit]:
        run_id = int(row["id"])
        captures = store.captures(run_id)
        failed = sum(1 for capture in captures if capture.failure)
        pending = len(review_module.outstanding(store, run_id))
        state = "complete" if row["complete"] else f"{pending} unanswered"
        print(
            f"{run_id:>4}  {row['started_at']}  {row['mode']:<5}  {row['commit_hash']:<12}  "
            f"{len(captures)} capture(s), {failed} failed, {state}"
        )

    return 0


# --- Wiring -----------------------------------------------------------------


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="test-suite", description=__doc__)
    parser.add_argument(
        "--database",
        type=Path,
        default=None,
        help="Store to use. Defaults to the XDG data directory.",
    )

    # The same option again on every subcommand, because `test-suite capture
    # --database ...` is what anyone would type and refusing it is a papercut.
    # SUPPRESS rather than a default, so a value given before the subcommand is
    # not silently overwritten by the subcommand's unset copy.
    shared = argparse.ArgumentParser(add_help=False)
    shared.add_argument("--database", type=Path, default=argparse.SUPPRESS)

    subparsers = parser.add_subparsers(
        dest="command",
        required=False,
        parser_class=lambda **kwargs: argparse.ArgumentParser(parents=[shared], **kwargs),
    )

    def add_run_option(target: argparse.ArgumentParser) -> None:
        target.add_argument("--run", type=int, default=None, help="Run id (default: the newest).")

    hub_parser = subparsers.add_parser("hub", help="The hub (also what a bare `test-suite` opens).")
    hub_parser.add_argument("--port", type=int, default=8730)
    hub_parser.add_argument("--no-browser", action="store_true")
    add_run_option(hub_parser)
    hub_parser.set_defaults(handler=command_hub)

    run_parser = subparsers.add_parser("run", help="Run suites without a browser.")
    run_parser.add_argument("--suites", nargs="+", default=[],
                            choices=[suite.id for suite in suites_module.SUITES])
    run_parser.add_argument("--all", action="store_true", help="Every suite.")
    run_parser.add_argument("--kind", choices=list(suites_module.KINDS))
    run_parser.add_argument("--mode", choices=(surfaces.QUICK, surfaces.FULL), default=surfaces.FULL)
    run_parser.add_argument("--resolutions", nargs="+", default=list(surfaces.DEFAULT_RESOLUTIONS),
                            choices=list(surfaces.SWEEP_GEOMETRIES))
    run_parser.add_argument("--rebuild", action="store_true")
    add_run_option(run_parser)
    run_parser.set_defaults(handler=command_run)

    capture_parser = subparsers.add_parser("capture", help="Photograph every surface, unattended.")
    capture_parser.add_argument("--executable", type=Path, default=DEFAULT_EXECUTABLE)
    capture_parser.add_argument("--mode", choices=(surfaces.QUICK, surfaces.FULL), default=surfaces.FULL)
    capture_parser.add_argument(
        "--resolutions",
        nargs="+",
        default=list(surfaces.DEFAULT_RESOLUTIONS),
        choices=list(surfaces.SWEEP_GEOMETRIES),
        help="Which window sizes to capture each surface at.",
    )
    capture_parser.add_argument("--build-config", default="", help="How the build under test was configured.")
    capture_parser.add_argument("--audio-device", default="", help="The input device in use.")
    capture_parser.add_argument("--window-title", default=None, help="Match a specific window title.")
    capture_parser.add_argument(
        "--run", type=int, default=None, help="Resume a run instead of starting one."
    )
    capture_parser.set_defaults(handler=command_capture)

    attend_parser = subparsers.add_parser(
        "attend", help="Ask the questions an image cannot answer, against a live application."
    )
    attend_parser.add_argument("--executable", type=Path, default=DEFAULT_EXECUTABLE)
    add_run_option(attend_parser)
    attend_parser.set_defaults(handler=command_attend)

    review_parser = subparsers.add_parser("review", help="Open the grid for a captured run.")
    review_parser.add_argument("--port", type=int, default=8730)
    review_parser.add_argument("--no-browser", action="store_true")
    add_run_option(review_parser)
    review_parser.set_defaults(handler=command_review)

    ingest_parser = subparsers.add_parser(
        "ingest", help="Attach a Performance Lab export or an automated suite result."
    )
    ingest_parser.add_argument("--performance", type=Path, default=None)
    ingest_parser.add_argument("--suite", default="")
    ingest_parser.add_argument("--report", type=Path, default=None)
    ingest_parser.add_argument("--cases", type=int, default=None)
    ingest_parser.add_argument("--failures", type=int, default=None)
    add_run_option(ingest_parser)
    ingest_parser.set_defaults(handler=command_ingest)

    export_parser = subparsers.add_parser("export", help="Write the record the release gate reads.")
    export_parser.add_argument("--directory", type=Path, default=None)
    add_run_option(export_parser)
    export_parser.set_defaults(handler=command_export)

    sync_parser = subparsers.add_parser(
        "sync", help="Write this machine's runs into the git-tracked history."
    )
    sync_parser.add_argument("--directory", type=Path, default=None)
    sync_parser.set_defaults(handler=command_sync)

    history_parser = subparsers.add_parser("history", help="Runs and measurements over time.")
    history_parser.add_argument("--directory", type=Path, default=None)
    history_parser.add_argument("--machine", default="", help="Defaults to this machine.")
    history_parser.add_argument("--limit", type=int, default=15)
    history_parser.set_defaults(handler=command_history)

    prune_parser = subparsers.add_parser("prune", help="Delete old images, keeping every decision.")
    prune_parser.add_argument("--keep", type=int, default=5)
    prune_parser.set_defaults(handler=command_prune)

    status_parser = subparsers.add_parser("status", help="What is in the store.")
    status_parser.add_argument("--limit", type=int, default=10)
    status_parser.set_defaults(handler=command_status)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    arguments = parser.parse_args(argv)

    # No subcommand means the hub. Typing the program's name should get you
    # somewhere useful, not a usage message.
    if getattr(arguments, "handler", None) is None:
        arguments = parser.parse_args(["hub", *(argv or [])])

    try:
        return arguments.handler(arguments)
    except StoreError as error:
        print(str(error), file=sys.stderr)

        return 1


if __name__ == "__main__":
    raise SystemExit(main())
