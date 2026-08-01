#!/usr/bin/env python3
"""Speaks the test control protocol to a running Practice Takes process.

The application must be built with ``-DPRACTICE_TAKES_ENABLE_TEST_CONTROL=ON``;
the channel is absent otherwise, which is deliberate — it is a control surface
over application state and must never ship.

Nothing here synthesises input. The driver names an approved state or an
approved object and the application does the rest, so a layout change cannot
silently redirect a click and a renamed state fails loudly instead of quietly
verifying the wrong thing.

Standard library only, so the protocol can be tested without Textual.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import subprocess

# Replies end with a verdict line, so a reader knows a reply is complete without
# counting bytes or waiting on a timeout.
OK = "ok"
ERROR_PREFIX = "error "
ITEM_PREFIX = "item "


class ChannelError(RuntimeError):
    """The application refused a command, or the channel broke."""


@dataclass
class Reply:
    success: bool
    items: list[str]
    error: str = ""


def parse_reply(lines: list[str]) -> Reply:
    """Turn raw reply lines into a verdict plus payload."""
    items = [line[len(ITEM_PREFIX):] for line in lines if line.startswith(ITEM_PREFIX)]

    for line in lines:
        if line == OK:
            return Reply(True, items)

        if line.startswith(ERROR_PREFIX):
            return Reply(False, items, line[len(ERROR_PREFIX):])

    return Reply(False, items, "the application gave no verdict")


class ApplicationDriver:
    """Launches the application and drives it through the control channel."""

    def __init__(self, executable: Path, extra_arguments: tuple[str, ...] = ()) -> None:
        self.executable = executable
        self.extra_arguments = extra_arguments
        self._process: subprocess.Popen[str] | None = None

    def start(self) -> None:
        if not self.executable.is_file():
            raise ChannelError(
                f"No application at {self.executable}. Build it with "
                f"-DPRACTICE_TAKES_ENABLE_TEST_CONTROL=ON first."
            )

        self._process = subprocess.Popen(
            [str(self.executable), "--test-control", *self.extra_arguments],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            bufsize=1,
        )

    def stop(self) -> None:
        """Ask the application to quit, then make sure it actually did."""
        process = self._process

        if process is None:
            return

        self._process = None

        try:
            if process.poll() is None:
                self.send("quit")
        except ChannelError:
            pass

        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            # A hung application must not hang the harness with it.
            process.kill()
            process.wait(timeout=5)

    def send(self, command: str) -> Reply:
        """Send one command and read its reply."""
        process = self._process

        if process is None or process.stdin is None or process.stdout is None:
            raise ChannelError("The control channel is not open.")

        if process.poll() is not None:
            raise ChannelError("The application exited.")

        process.stdin.write(command + "\n")
        process.stdin.flush()

        lines: list[str] = []

        while True:
            line = process.stdout.readline()

            if not line:
                raise ChannelError(
                    f"The application closed the channel while answering '{command}'."
                )

            stripped = line.rstrip("\n")
            lines.append(stripped)

            if stripped == OK or stripped.startswith(ERROR_PREFIX):
                return parse_reply(lines)

    # --- The vocabulary -----------------------------------------------------

    def list_states(self) -> list[str]:
        reply = self.send("list-states")

        if not reply.success:
            raise ChannelError(reply.error)

        # Each item is "<id> <description>".
        return [item.split(" ", 1)[0] for item in reply.items]

    def open_state(self, state: str) -> Reply:
        return self.send(f"open-state {state}")

    def status(self) -> str:
        reply = self.send("status")

        if not reply.success or not reply.items:
            raise ChannelError(reply.error or "status gave no answer")

        return reply.items[0]

    def restart(self) -> None:
        """Relaunch, for surfaces that verify something surviving a restart."""
        self.stop()
        self.start()

    @property
    def pid(self) -> int | None:
        return self._process.pid if self._process is not None else None


# Window geometry is done with the repository's existing X11 helper rather than
# through the control channel. Resizing a window is not input synthesis -- no
# pointer moves and no key is faked -- and window_control already does exactly
# this by PID for the golden-image harness.
WINDOW_CONTROL_SOURCE = Path("scripts/quality/ui-validation/window_control.cpp")

# Sizes the sweep presents. The constrained one is below MainTitleBar's 900px
# threshold, so it is also what makes the collapsed menu appear.
GEOMETRY_SIZES = {
    "default": ("resize", "1280", "800"),
    "constrained": ("resize", "800", "600"),
    "maximised": ("maximize",),
}


def build_window_control(repository_root: Path, output: Path) -> Path:
    """Compile the X11 helper if it is not already built."""
    if output.is_file():
        return output

    source = repository_root / WINDOW_CONTROL_SOURCE

    if not source.is_file():
        raise ChannelError(f"No window_control source at {source}")

    output.parent.mkdir(parents=True, exist_ok=True)

    completed = subprocess.run(
        ["g++", "-std=c++20", "-O2", str(source), "-o", str(output), "-lX11"],
        capture_output=True,
        text=True,
    )

    if completed.returncode != 0:
        raise ChannelError(f"Could not build window_control: {completed.stderr.strip()}")

    return output


def set_geometry(window_control: Path, pid: int, geometry: str) -> str:
    """Apply a named geometry. Returns a reason on failure, empty on success."""
    arguments = GEOMETRY_SIZES.get(geometry)

    if arguments is None:
        return f"unknown geometry '{geometry}'"

    completed = subprocess.run(
        [str(window_control), str(pid), *arguments],
        capture_output=True,
        text=True,
    )

    if completed.returncode != 0:
        return f"window_control failed: {completed.stderr.strip() or completed.returncode}"

    return ""


def missing_states(available: list[str], required: frozenset[str]) -> list[str]:
    """States the surface list needs that the application does not offer.

    Checked before a run starts. A harness that has drifted from the
    application should fail immediately, not after a tester has answered half
    the questions.
    """
    return sorted(required - set(available))
