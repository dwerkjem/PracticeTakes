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
import os
from pathlib import Path
import queue
import subprocess
import threading
import time

# Replies end with a verdict line, so a reader knows a reply is complete without
# counting bytes or waiting on a timeout.
OK = "ok"
ERROR_PREFIX = "error "
ITEM_PREFIX = "item "


class ChannelError(RuntimeError):
    """The application refused a command, or the channel broke."""


# How long one command may take before the channel is declared broken.
#
# Generous, because a state change legitimately takes seconds and a false
# timeout would report a working build as broken. Not unbounded, because an
# application that stops answering otherwise hangs the run with nothing said:
# that happened here with two instances running, where the second blocked
# inside ALSA opening a device the first already held, on the thread that
# answers this channel. The run waited on a pipe forever and no image, log
# line, or failed row said so.
REPLY_TIMEOUT_SECONDS = 60.0

# What `quit` gets. Shorter, because an application on its way out has nothing
# left to be slow about, and one that will not answer is about to be killed
# anyway -- waiting a minute first only delays the run's own shutdown.
QUIT_TIMEOUT_SECONDS = 5.0

# What the first command after a launch gets. Shorter than a general command,
# because this is the one that catches an application which came up but cannot
# open the input device -- and the answer to that is to start another, which is
# only worth doing promptly. A healthy launch answers in well under a second,
# and under contention in about six.
STARTUP_TIMEOUT_SECONDS = 12.0


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

    def __init__(
        self,
        executable: Path,
        extra_arguments: tuple[str, ...] = (),
        display: str = "",
    ) -> None:
        self.executable = executable
        self.extra_arguments = extra_arguments
        # Which X screen this application opens on. Empty means "inherit", which
        # is right for one application at a time. It stops being right the
        # moment there are two: `os.environ` is one dictionary per process, so
        # the second display started would silently become everybody's.
        self.display = display
        self._process: subprocess.Popen[str] | None = None

    def _environment(self) -> dict[str, str] | None:
        if not self.display:
            return None

        return {**os.environ, "DISPLAY": self.display}

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
            env=self._environment(),
        )

        # Read on a thread of its own so a reply can be waited for with a
        # deadline. `readline` on the pipe cannot be given one, and selecting on
        # the descriptor would miss a complete line already sitting in Python's
        # buffer.
        self._lines = queue.Queue()
        self._reader = threading.Thread(
            target=self._read_lines, args=(self._process.stdout,), daemon=True
        )
        self._reader.start()

    def _read_lines(self, stream) -> None:
        for line in stream:
            self._lines.put(line.rstrip("\n"))

        # Closed. A sentinel rather than silence, so a waiting `send` learns
        # about it now instead of at its deadline.
        self._lines.put(None)

    def kill(self) -> None:
        """End it now, without asking.

        For an application that has already failed to answer. `stop` is polite
        first -- five seconds for a reply, ten more for the exit -- and being
        polite to something known not to be listening is fifteen seconds spent
        proving it twice.
        """
        process = self._process

        if process is None:
            return

        self._process = None
        process.kill()

        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            pass

        for stream in (process.stdin, process.stdout):
            if stream is not None:
                try:
                    stream.close()
                except OSError:
                    pass

    def stop(self) -> None:
        """Ask the application to quit, then make sure it actually did."""
        process = self._process

        if process is None:
            return

        try:
            if process.poll() is None:
                # A short deadline of its own, not the one a real command gets.
                # An application being shut down has nothing left to be slow
                # about, and the case where it does not answer is exactly the
                # one this is not going to wait a minute for.
                self.send("quit", timeout=QUIT_TIMEOUT_SECONDS)
        except ChannelError:
            pass
        finally:
            # Cleared after asking, not before. Clearing first made `send` raise
            # "the control channel is not open" against this very process, so
            # `quit` was never sent and every stop waited ten seconds and then
            # killed the application -- which also meant it never left the way a
            # user's would.
            self._process = None

        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            # A hung application must not hang the harness with it.
            process.kill()
            process.wait(timeout=5)

        for stream in (process.stdin, process.stdout):
            if stream is not None:
                try:
                    stream.close()
                except OSError:
                    pass

    def send(self, command: str, timeout: float | None = None) -> Reply:
        """Send one command and read its reply, or say that none came."""
        process = self._process

        if process is None or process.stdin is None or process.stdout is None:
            raise ChannelError("The control channel is not open.")

        if process.poll() is not None:
            raise ChannelError("The application exited.")

        process.stdin.write(command + "\n")
        process.stdin.flush()

        deadline = time.monotonic() + (
            REPLY_TIMEOUT_SECONDS if timeout is None else timeout
        )
        lines: list[str] = []

        while True:
            remaining = deadline - time.monotonic()

            if remaining <= 0:
                raise ChannelError(
                    f"The application did not answer '{command}' within "
                    f"{REPLY_TIMEOUT_SECONDS if timeout is None else timeout:g}s. "
                    f"It is running but not responding."
                )

            try:
                line = self._lines.get(timeout=remaining)
            except queue.Empty:
                continue

            if line is None:
                raise ChannelError(
                    f"The application closed the channel while answering '{command}'."
                )

            lines.append(line)

            if line == OK or line.startswith(ERROR_PREFIX):
                return parse_reply(lines)

    # --- The vocabulary -----------------------------------------------------

    def list_states(self, timeout: float | None = None) -> list[str]:
        reply = self.send("list-states", timeout=timeout)

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

    def set_theme(self, theme: str) -> str:
        """Switch the palette. Returns a reason on failure, empty on success."""
        reply = self.send(f"theme {theme}")

        return "" if reply.success else reply.error

    def set_geometry(self, geometry: str) -> str:
        """Apply a sweep geometry. Returns a reason on failure, empty on success."""
        name = GEOMETRY_NAMES.get(geometry)

        if name is None:
            return f"unknown geometry '{geometry}'"

        reply = self.send(f"geometry {name}")

        return "" if reply.success else reply.error


# Geometry names the application accepts, mapped from the sweep's names.
#
# Applied through the control channel rather than by an external resize. The
# window advertises a 980px minimum width and a window manager honours it, but
# that is above the 900px threshold at which the title bar collapses -- so an
# outside resize could never reach the collapsed menu. Measured: an external
# resize to 800x600 left the window at 1280x800.
GEOMETRY_NAMES = {
    "default": "normal",
    "constrained": "narrow",
    "slim": "slim",
    "squat": "squat",
    "tiny": "tiny",
    "maximised": "maximised",
}


def missing_states(available: list[str], required: frozenset[str]) -> list[str]:
    """States the surface list needs that the application does not offer.

    Checked before a run starts. A harness that has drifted from the
    application should fail immediately, not after a tester has answered half
    the questions.
    """
    return sorted(required - set(available))
