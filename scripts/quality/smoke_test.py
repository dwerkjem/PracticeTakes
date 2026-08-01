#!/usr/bin/env python3
"""End-to-end smoke tests: does the built application actually run?

Launches the real executable and asserts the three things 300-odd unit tests
cannot tell you — that it starts, that a tool opens, and that it shuts down
cleanly.

A script rather than a Catch2 target, deliberately. The assertions are about
process lifecycle, not values: a hung child needs a supervisor with a timeout,
which a test framework does not provide, and nothing here should link against
the application.

Needs a build with ``-DPRACTICE_TAKES_ENABLE_TEST_CONTROL=ON`` and a display.
On a headless machine, run it under Xvfb:

    xvfb-run -a python3 scripts/quality/smoke_test.py

Standard library only.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys
import time

REPOSITORY_ROOT = Path(__file__).resolve().parents[2]

sys.path.insert(0, str(REPOSITORY_ROOT / "scripts" / "manual_gui"))

from driver import ApplicationDriver, ChannelError  # noqa: E402

DEFAULT_EXECUTABLE = REPOSITORY_ROOT / "build-tc" / "bin" / "PracticeTakes"

# Generous, because this has to hold on a loaded CI runner as well as a
# developer's machine. The point is to fail rather than hang forever, not to
# measure how fast startup is -- ApplicationLaunchTimer does that.
STARTUP_TIMEOUT_SECONDS = 60
SHUTDOWN_TIMEOUT_SECONDS = 30


class SmokeFailure(RuntimeError):
    pass


def report(name: str, detail: str = "") -> None:
    print(f"  ok  {name}" + (f" — {detail}" if detail else ""), flush=True)


def check_starts(driver: ApplicationDriver) -> None:
    """It reaches a running state with its main window created."""
    started = time.monotonic()
    driver.start()

    # The channel only answers once the message loop is running and the window
    # exists, so a reply *is* the evidence of a successful start.
    while True:
        if time.monotonic() - started > STARTUP_TIMEOUT_SECONDS:
            raise SmokeFailure(
                f"The application did not become responsive within "
                f"{STARTUP_TIMEOUT_SECONDS}s."
            )

        try:
            driver.list_states()
            break
        except ChannelError as error:
            raise SmokeFailure(f"The application exited during startup: {error}") from error

    report("starts", f"responsive in {time.monotonic() - started:.1f}s")


def check_opens_a_tool(driver: ApplicationDriver) -> None:
    """The shell, a tool component, and their wiring survive real construction."""
    reply = driver.open_state("tuner-docked")

    if not reply.success:
        raise SmokeFailure(f"Could not open a tool: {reply.error}")

    # Confirmed rather than assumed: open-state reporting success and the
    # application actually being in that state are different claims.
    if driver.status() != "tuner-docked":
        raise SmokeFailure("The application did not report the state it was asked for.")

    report("opens a tool")


def check_shuts_down(driver: ApplicationDriver) -> None:
    """It exits within a bounded time, and the process is gone."""
    process = driver._process  # noqa: SLF001 - the supervisor needs the handle

    if process is None:
        raise SmokeFailure("The application was not running.")

    started = time.monotonic()
    driver.send("quit")

    try:
        code = process.wait(timeout=SHUTDOWN_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired as error:
        process.kill()
        process.wait(timeout=5)

        raise SmokeFailure(
            f"The application did not exit within {SHUTDOWN_TIMEOUT_SECONDS}s; killed it."
        ) from error
    finally:
        driver._process = None  # noqa: SLF001

    if code != 0:
        raise SmokeFailure(f"The application exited with status {code}.")

    report("shuts down cleanly", f"in {time.monotonic() - started:.1f}s")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, default=DEFAULT_EXECUTABLE)
    arguments = parser.parse_args(argv)

    print(f"Smoke testing {arguments.executable}")

    driver = ApplicationDriver(arguments.executable)

    try:
        check_starts(driver)
        check_opens_a_tool(driver)
        check_shuts_down(driver)
    except (SmokeFailure, ChannelError) as failure:
        print(f"\nFAILED: {failure}", file=sys.stderr)

        # Never leave a child behind, however the run ended.
        driver.stop()

        return 1

    print("\nAll smoke tests passed.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
