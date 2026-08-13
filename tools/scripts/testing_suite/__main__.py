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
from contextlib import contextmanager, ExitStack
from pathlib import Path
import platform
import subprocess
import sys
import tempfile
import threading
import time
import webbrowser

sys.path.insert(0, str(Path(__file__).resolve().parent))

import attend as attend_module  # noqa: E402
import capture as capture_module  # noqa: E402
import display as display_module  # noqa: E402
import freshness as freshness_module  # noqa: E402
import runner as runner_module  # noqa: E402
import suites as suites_module  # noqa: E402
import export as export_module  # noqa: E402
import fleet as fleet_module  # noqa: E402
import history as history_module  # noqa: E402
import ingest as ingest_module  # noqa: E402
import launcher as launcher_module  # noqa: E402
import machine as machine_module  # noqa: E402
import review as review_module  # noqa: E402
import server as server_module  # noqa: E402
import surfaces  # noqa: E402
from driver import (  # noqa: E402
    STARTUP_TIMEOUT_SECONDS,
    ApplicationDriver,
    ChannelError,
    missing_states,
)
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
    themes = tuple(arguments.themes)
    try:
        plan = surfaces.plan(arguments.mode, resolutions, themes, tuple(arguments.surfaces))
    except ValueError as error:
        print(str(error), file=sys.stderr)

        return 1

    # Said before anything is captured, so it is read rather than scrolled past
    # at the end alongside a summary that says everything worked.
    warning = freshness_module.staleness_warning(arguments.executable, REPOSITORY_ROOT)

    if warning:
        print(warning, file=sys.stderr)

    if arguments.scratch:
        # Both name where the run is written, and one of them would silently win.
        if getattr(arguments, "database", None) is not None:
            print("--scratch and --database both choose a store; pick one.", file=sys.stderr)

            return 1

        # Nothing to resume into: a scratch store is empty by construction.
        if arguments.run:
            print(
                f"--scratch starts a new store, so there is no run {arguments.run} in it "
                f"to resume.",
                file=sys.stderr,
            )

            return 1

        # A throwaway store, so a look-at-this capture does not become run 47 in
        # a history meant for verification passes. The directory is deliberately
        # not cleaned up on exit: the images have to outlive the process for
        # anyone to look at them, which is the entire point. It is under the
        # system temporary directory, so the machine clears it eventually.
        arguments.database = (
            Path(tempfile.mkdtemp(prefix="practice-takes-capture-")) / "verification.db"
        )
        print("Scratch run; the verification history is untouched.")

    store = open_store(arguments)

    if arguments.workers < 1:
        print("--workers needs at least 1.", file=sys.stderr)

        return 1

    if arguments.workers > 1:
        if arguments.headless is False:
            # Several applications resizing themselves on one screen is the
            # arrangement this was impossible under before headless existed.
            print(
                "--workers needs a display per worker, so it cannot run on the "
                "desktop display. Drop --no-headless.",
                file=sys.stderr,
            )

            return 1

        if not display_module.is_available():
            print(display_module.missing_message(), file=sys.stderr)

            return 1

        return _capture_in_parallel(arguments, plan, store, resolutions)

    # Entered before anything opens a window, and left after the driver stops,
    # so every process this run starts inherits the private DISPLAY.
    with ExitStack() as stack:
        # Unset means the default rather than a request, and the two differ when
        # Xvfb is missing: refusing is right for somebody who asked for a
        # private screen, and wrong for somebody who never mentioned it and
        # would rather have their capture than a lecture.
        requested = arguments.headless is True
        wanted = arguments.headless is not False

        if wanted:
            try:
                screen = stack.enter_context(display_module.virtual_display())
                print(f"Capturing on a virtual display ({screen}); nothing will appear on screen.")
            except display_module.VirtualDisplayError as error:
                if requested:
                    print(str(error), file=sys.stderr)

                    return 1

                print(f"{error}\nFalling back to the desktop display.", file=sys.stderr)

        return _capture_on_current_display(arguments, plan, store, resolutions)


# One. A second attempt is made while holding the gate every other worker is
# waiting on, and the thing that makes a launch fail -- other instances already
# holding the input device -- is not something a retry changes. Trying twice
# turned one bad launch into seventy seconds of everybody waiting, which was
# slower than not running in parallel at all. A worker that cannot start
# retires instead, and the queue it never drew from goes to the others.
STARTUP_ATTEMPTS = 1

# How long a worker waits to see whether it got the input device. Short, because
# one application holds it and the others will not get it however long they
# wait; what the answer is for is routing the surfaces that need to hear
# something to the worker that can.
WORKER_INPUT_WAIT_SECONDS = 4.0


def _start_under(driver, gate, fleet) -> list[str]:
    """Start an application while holding the audio gate, retrying a bad launch.

    Returns the states it offers, which doubles as proof that it is answering:
    an application that cannot open the input device comes up, draws a window,
    and never replies.
    """
    last: ChannelError | None = None

    for _ in range(STARTUP_ATTEMPTS):
        failed = False

        with gate:
            driver.start()
            # Registered the moment it exists, so teardown can end it whatever
            # happens next -- and so nothing ends it by name.
            fleet.add(driver)

            try:
                return driver.list_states(timeout=STARTUP_TIMEOUT_SECONDS)
            except ChannelError as error:
                last = error
                failed = True

        # Killed outside the gate, and killed rather than asked. Every worker
        # behind this one waits for the gate, and spending five seconds asking
        # a process that is known not to answer -- then ten more waiting for it
        # to go -- spends that on all of them. This is what made a run with one
        # bad launch take longer than the whole sequential pass.
        if failed:
            driver.kill()
            fleet.remove(driver)

    raise ChannelError(
        f"The application would not start after {STARTUP_ATTEMPTS} attempts: {last}"
    )


def _capture_in_parallel(arguments, plan, store, resolutions) -> int:
    """One plan, several screens, one run.

    Each worker gets a screen and an application of its own, and passes the
    screen's name to both explicitly. Nothing here relies on inheriting
    `DISPLAY`: there is one of those per process and there are several screens,
    so a worker that inherited would photograph whichever came up last.
    """
    try:
        tooling = capture_module.Tooling.ensure()
    except capture_module.CaptureError as error:
        print(str(error), file=sys.stderr)

        return 1

    run_id = int(store.run(int(arguments.run))["id"]) if arguments.run else store.start_run(
        provenance=machine_module.provenance(),
        commit=current_commit(),
        mode=arguments.mode,
        resolutions=resolutions,
        build_config=arguments.build_config,
        platform=describe_platform(),
        audio_device=arguments.audio_device,
    )
    images_at = image_directory(store, run_id)
    fleet = fleet_module.Fleet()

    # Said before anything starts. An application somebody else is running holds
    # the input device, and a worker that cannot open one comes up, draws a
    # window, and never replies -- which reads as a broken build rather than as
    # a busy machine.
    warning = fleet_module.contention_warning(fleet.foreign())

    if warning:
        print(warning, file=sys.stderr)

    @contextmanager
    def worker(index: int, shared):
        with display_module.virtual_display(publish=False) as screen:
            driver = ApplicationDriver(arguments.executable, display=screen)

            # One application opens the input device at a time. Held until this
            # one answers, which is the proof that it is past the open rather
            # than a guess at how long one takes -- and released before any
            # capturing, because the device is only contended while it is being
            # acquired.
            #
            # Tried more than once, because the gate cannot cover everything:
            # the application reopens the device from a timer of its own when it
            # decides it has none, and that can land while another worker holds
            # it. One that came up without a device is not going to find one, so
            # it is replaced rather than waited on.
            try:
                states = _start_under(driver, shared.audio, fleet)
                # Before any capturing. The device opens without blocking now,
                # so a worker can be answering commands while its application
                # still has nothing to hear -- and the surfaces that carry a
                # tone would photograph a tool with an empty history.
                # Short: one application holds the device and the rest will
                # not get it however long they wait. What matters is knowing
                # which this worker is, so the tone surfaces can go to one that
                # can hear.
                analyses = driver.wait_for_input(timeout=WORKER_INPUT_WAIT_SECONDS)

                absent = missing_states(states, surfaces.required_states())

                if absent:
                    raise capture_module.CaptureError(
                        "the application does not offer: " + ", ".join(absent)
                    )

                made = capture_module.CapturePass(
                    store=store,
                    run_id=run_id,
                    driver=driver,
                    tooling=tooling,
                    image_directory=images_at,
                    window_title=arguments.window_title,
                    display=screen,
                    lock=shared.checks,
                    audio_gate=shared.audio,
                )
                made.has_input = analyses

                yield made
            finally:
                driver.stop()
                fleet.remove(driver)

    began = time.monotonic()

    def say(entry: dict) -> None:
        left = runner_module.remaining_seconds(entry, time.monotonic() - began)
        print(
            f"[{entry['done'] + 1}/{entry['total']}] {entry['surface']} "
            f"at {entry['geometry']} in {entry.get('theme', 'dark')}"
            f"{' — ' + runner_module.describe_remaining(left) if left is not None else ''}"
        )

    print(f"Capturing on {arguments.workers} screens at once; nothing will appear on yours.")

    try:
        result = capture_module.run_in_parallel(
            plan, workers=arguments.workers, worker=worker, progress=say
        )
    finally:
        # Only what this run started. An interrupted run used to leave
        # applications behind holding the input device, which made every run
        # after it slower for reasons nothing explained.
        left = fleet.shut_down()

        if left:
            print(f"Ended {left} application(s) this run had left.", file=sys.stderr)

    for error in result["errors"]:
        print(f"A worker stopped: {error}", file=sys.stderr)

    print(
        f"Run {run_id}: {result['captured']} captured, {result['failed']} failed, "
        f"{result['already_captured']} already present, "
        f"across {result['workers']} worker(s)."
    )
    print(f"Images: {images_at}")

    return 1 if result["failed"] or result["errors"] else 0


def _capture_on_current_display(arguments, plan, store, resolutions) -> int:
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
    # The path, not just the review command: someone who captured two surfaces
    # to look at a change wants the files, not a browser grid.
    print(f"Images:          {image_directory(store, run_id)}")
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
        # Anything opened for a closer look goes with the hub; leaving a
        # test-control instance running after the tool that started it is a
        # surprise nobody needs. Impatience is expected here -- a second Ctrl-C
        # while this is closing should stop the tool, not print a traceback.
        try:
            httpd.session.launch.close()
        except KeyboardInterrupt:
            print("Left the application running.")

        httpd.server_close()

    return 0


def command_open(arguments) -> int:
    """Open a live application on one surface, for a closer look."""
    store = open_store(arguments)
    launch = launcher_module.ManualLaunch(arguments.executable)

    try:
        opened = launch.open(
            state=arguments.state, geometry=arguments.geometry, theme=arguments.theme
        )
    except launcher_module.LaunchError as error:
        print(str(error), file=sys.stderr)

        return 1

    print(f"{opened['state']} open in {opened['theme']}, {opened['geometry']} size.")
    print("Close the window, or press Enter here, to stop it.")

    try:
        input()
    except (EOFError, KeyboardInterrupt):
        pass
    finally:
        launch.close()

    del store

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
                     themes=tuple(arguments.themes), rebuild=arguments.rebuild,
                     run_id=arguments.run):
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

    # Not `data["runs"][-arguments.limit:]` unguarded: Python's `-0 == 0`, so a
    # limit of zero -- asking to see none -- sliced from index 0 and showed
    # every run instead of the last zero of them.
    shown = data["runs"][-arguments.limit :] if arguments.limit > 0 else []

    for run in shown:
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
    run_parser.add_argument("--themes", nargs="+", default=list(surfaces.DEFAULT_THEMES),
                            choices=list(surfaces.THEMES))
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
    capture_parser.add_argument(
        "--themes",
        nargs="+",
        default=list(surfaces.DEFAULT_THEMES),
        choices=list(surfaces.THEMES),
        help="Which palettes to capture each surface in.",
    )
    capture_parser.add_argument(
        "--surfaces",
        nargs="+",
        default=[],
        metavar="STATE",
        help="Capture only these surfaces, by approved-state name (for example "
        "tuner-in-tune). Default: every surface the mode covers. An unknown "
        "name is an error, not an empty run.",
    )
    capture_parser.add_argument(
        "--scratch",
        action="store_true",
        help="Write to a throwaway store under the system temporary directory "
        "instead of the verification history. For a capture you only need to "
        "look at once.",
    )
    # Default, because watching windows open and resize on your own screen is
    # the reason capture runs get put off. None rather than True so the code can
    # tell "asked for it" from "did not say", which decides whether a missing
    # Xvfb is an error or a fallback.
    capture_parser.add_argument(
        "--headless",
        action=argparse.BooleanOptionalAction,
        default=None,
        help="Capture on a private Xvfb screen instead of the desktop, so no "
        "windows appear and nothing steals focus. On by default; --no-headless "
        "captures on the desktop.",
    )
    capture_parser.add_argument(
        "--workers",
        type=int,
        default=1,
        help="How many screens to capture on at once. Default 1; 2 is the most "
        "that works today. Never derived from the processor count. Above two, "
        "instances contend for the input device and one blocks inside ALSA "
        "opening it, on the thread that answers the control channel — so the "
        "application runs but stops answering, and its share is recorded as "
        "failures.",
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

    open_parser = subparsers.add_parser(
        "open", help="Open a live application on one surface, for a closer look."
    )
    open_parser.add_argument("state", help="An approved state, as named in surfaces.py.")
    open_parser.add_argument("--executable", type=Path, default=DEFAULT_EXECUTABLE)
    open_parser.add_argument("--geometry", default="", choices=["", *surfaces.SWEEP_GEOMETRIES])
    open_parser.add_argument("--theme", default="", choices=["", *surfaces.THEMES])
    open_parser.set_defaults(handler=command_open)

    prune_parser = subparsers.add_parser("prune", help="Delete old images, keeping every decision.")
    prune_parser.add_argument("--keep", type=int, default=5)
    prune_parser.set_defaults(handler=command_prune)

    status_parser = subparsers.add_parser("status", help="What is in the store.")
    status_parser.add_argument("--limit", type=int, default=10)
    status_parser.set_defaults(handler=command_status)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    # What was actually asked for: argparse itself falls back to sys.argv[1:]
    # when `argv` is None, which the real console-script entry point always
    # passes. `argv or []` below used to read that same None and silently
    # produce `[]`, dropping any global option -- `--database`, most notably
    # -- typed before a bare invocation with no subcommand.
    given = sys.argv[1:] if argv is None else argv
    arguments = parser.parse_args(given)

    # No subcommand means the hub. Typing the program's name should get you
    # somewhere useful, not a usage message.
    if getattr(arguments, "handler", None) is None:
        arguments = parser.parse_args(["hub", *given])

    try:
        return arguments.handler(arguments)
    except StoreError as error:
        print(str(error), file=sys.stderr)

        return 1


if __name__ == "__main__":
    raise SystemExit(main())
