#!/usr/bin/env python3
"""Run a manual GUI verification session.

    python3 scripts/manual_gui --quick
    python3 scripts/manual_gui --full --geometry-sweep

Needs an application built with -DPRACTICE_TAKES_ENABLE_TEST_CONTROL=ON, a real
display, a real audio device, and a person. No CI check depends on it.
"""

from __future__ import annotations

import argparse
import datetime as _datetime
from pathlib import Path
import platform
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))

import record  # noqa: E402
import session as session_module  # noqa: E402
import surfaces  # noqa: E402
from driver import ApplicationDriver, ChannelError, missing_states  # noqa: E402

REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
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


def run_check(arguments) -> int:
    """Prove the harness can drive the application, without asking anybody anything."""
    driver = ApplicationDriver(arguments.executable)

    try:
        driver.start()
    except ChannelError as error:
        print(str(error), file=sys.stderr)

        return 1

    try:
        available = driver.list_states()
        absent = missing_states(available, surfaces.required_states())

        if absent:
            print("Missing states:\n  " + "\n  ".join(absent), file=sys.stderr)

            return 1

        # Actually enter every surface, rather than only checking the names
        # exist. A state that is offered but cannot be established would
        # otherwise only surface mid-run, in front of a tester.
        failures = []

        for surface in surfaces.SURFACES:
            reply = driver.open_state(surface.state)

            if not reply.success:
                failures.append(f"{surface.title}: {reply.error}")
            elif driver.status() != surface.state:
                failures.append(f"{surface.title}: status did not confirm the state")

        if failures:
            print("Surfaces that could not be reached:", file=sys.stderr)

            for failure in failures:
                print(f"  - {failure}", file=sys.stderr)

            return 1

        print(
            f"OK: {len(available)} states offered, "
            f"{len(surfaces.SURFACES)} surfaces all reachable."
        )
    finally:
        driver.stop()

    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--full",
        action="store_const",
        const=surfaces.FULL,
        dest="mode",
        help="Every surface and question. Run before a release.",
    )
    mode.add_argument(
        "--quick",
        action="store_const",
        const=surfaces.QUICK,
        dest="mode",
        help="A reduced set answering only whether the application still works.",
    )
    parser.add_argument(
        "--geometry-sweep",
        action="store_true",
        help="Present each surface at several window sizes. Off by default "
        "because it multiplies how many prompts a run asks.",
    )
    parser.add_argument("--executable", type=Path, default=DEFAULT_EXECUTABLE)
    parser.add_argument(
        "--audio-device",
        default="unspecified",
        help="Recorded with the run, so a result is attributable to a device.",
    )
    parser.add_argument(
        "--records-directory",
        type=Path,
        default=REPOSITORY_ROOT / record.DEFAULT_RECORDS_DIRECTORY,
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Validate the surface list, launch the application, confirm every "
        "state it needs exists, then exit without prompting. Lets the harness "
        "be verified without a tester.",
    )
    parser.set_defaults(mode=surfaces.FULL)
    arguments = parser.parse_args(argv)

    # A malformed surface list must fail now, not partway through a run.
    problems = surfaces.validate()

    if problems:
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)

        return 1

    if arguments.check:
        return run_check(arguments)

    try:
        from app import VerificationApp
    except ImportError:
        # Naming the install command alone is not enough: `uv sync` installs
        # into .venv, and a bare `python3` does not look there, so following
        # only half of this lands you right back here.
        interpreter = Path(sys.executable)
        in_venv = (REPOSITORY_ROOT / ".venv") in interpreter.parents

        print(
            f"Textual is not installed for this interpreter ({interpreter}).\n"
            f"\n"
            f"Run the harness through uv, which installs and runs in one step:\n"
            f"\n"
            f"    uv run scripts/manual_gui --quick\n",
            file=sys.stderr,
        )

        if not in_venv and (REPOSITORY_ROOT / ".venv").is_dir():
            print(
                f"There is already a .venv here; you are just not using it. "
                f"Either use the command above, or:\n"
                f"\n"
                f"    {REPOSITORY_ROOT / '.venv' / 'bin' / 'python3'} scripts/manual_gui --quick\n",
                file=sys.stderr,
            )

        return 1

    driver = ApplicationDriver(arguments.executable)

    try:
        driver.start()
    except ChannelError as error:
        print(str(error), file=sys.stderr)

        return 1

    run = record.Run(
        commit=current_commit(),
        platform=describe_platform(),
        audio_device=arguments.audio_device,
        mode=arguments.mode,
        geometry_sweep=arguments.geometry_sweep,
        started_at=_datetime.datetime.now().replace(microsecond=0).isoformat(),
    )
    active = session_module.Session(run, arguments.mode, arguments.geometry_sweep)

    try:
        # Check the vocabulary before relying on it, so a harness that has
        # drifted from the application says so up front.
        absent = missing_states(driver.list_states(), surfaces.required_states())

        if absent:
            print(
                "The application does not offer these states the surface list "
                "needs:\n  " + "\n  ".join(absent),
                file=sys.stderr,
            )

            return 1

        def move(surface: surfaces.Surface, geometry: str) -> str:
            """Put the application on a surface. Returns a reason on failure."""
            try:
                if surface.restart_before:
                    driver.restart()

                reply = driver.open_state(surface.state)

                if not reply.success:
                    return reply.error

                if arguments.geometry_sweep:
                    return driver.set_geometry(geometry)

                return ""
            except ChannelError as error:
                return str(error)

        VerificationApp(active, move).run()
    finally:
        active.finish()
        driver.stop()
        run.finished_at = _datetime.datetime.now().replace(microsecond=0).isoformat()

        json_path, markdown_path = record.write(run, arguments.records_directory)

        status = "complete" if run.complete else "INCOMPLETE"
        print(f"\n{status} run recorded:\n  {json_path}\n  {markdown_path}")

        failures = run.failures()

        if failures:
            print(f"\n{len(failures)} failure(s):")

            for failure in failures:
                print(f"  - {failure.surface}: {failure.prompt} — {failure.note}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
