#!/usr/bin/env python3
"""A private X display to photograph the application on.

A capture run opens windows, resizes them four times each, and switches palette
between passes. On the desktop display that is minutes of windows flashing over
whatever the person at the keyboard was doing, and it is why capture runs get
put off until nobody is using the machine.

Xvfb gives the run its own screen. Nothing appears, nothing steals focus, and
the captured pixels are identical -- `xwindow_capture` reads a window's contents
from the X server, so it neither knows nor cares that the screen is not attached
to a monitor.

Not a substitute for looking at the real thing on real hardware. Font hinting
and compositing can differ, and `attend` still needs a display a person can see.
This is for the pass that only produces images.

Standard library only.
"""

from __future__ import annotations

from contextlib import contextmanager
import os
from pathlib import Path
import shutil
import socket
import subprocess
import time
from typing import Iterator

# Big enough that the "maximised" geometry means something: it asks JUCE for the
# primary display's user area, which under Xvfb is exactly this. A screen the
# size of a small laptop would quietly turn the widest capture into the default
# one.
DEFAULT_WIDTH = 1920
DEFAULT_HEIGHT = 1200
DEFAULT_DEPTH = 24

# Display numbers below this are where real sessions live.
FIRST_DISPLAY_NUMBER = 90
LAST_DISPLAY_NUMBER = 120

READY_TIMEOUT_SECONDS = 10.0
READY_POLL_SECONDS = 0.1

X11_SOCKET_DIRECTORY = Path("/tmp/.X11-unix")


class VirtualDisplayError(RuntimeError):
    """Xvfb is missing, or refused to start."""


def is_available() -> bool:
    """Whether Xvfb is installed."""
    return shutil.which("Xvfb") is not None


def missing_message() -> str:
    return (
        "Xvfb is not installed, so there is no display to capture on. Install it "
        "with `bash tools/scripts/build/check-linux-build-dependencies.sh --install`, "
        "or drop --headless to capture on the desktop display."
    )


def display_in_use(number: int) -> bool:
    """Whether an X server already answers on this display number.

    The socket file is checked as well as connected to: a stale socket left by a
    crashed server would otherwise look free right up until Xvfb refused to bind
    it.
    """
    if (X11_SOCKET_DIRECTORY / f"X{number}").exists():
        return True

    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as probe:
        probe.settimeout(0.2)
        try:
            probe.connect(f"\0/tmp/.X11-unix/X{number}")
        except OSError:
            return False

    return True


def find_free_display(
    first: int = FIRST_DISPLAY_NUMBER, last: int = LAST_DISPLAY_NUMBER
) -> int:
    """The lowest unused display number in the private range."""
    for number in range(first, last + 1):
        if not display_in_use(number):
            return number

    raise VirtualDisplayError(
        f"No free X display between :{first} and :{last}. Something is leaking "
        f"X servers; check for stray Xvfb processes."
    )


def wait_until_ready(
    number: int, process: subprocess.Popen, timeout: float = READY_TIMEOUT_SECONDS
) -> None:
    """Block until the server accepts connections, or explain why it never will.

    Polling the socket rather than sleeping a fixed interval: Xvfb is usually up
    in well under a second, and a fixed sleep long enough to be safe on a loaded
    machine is wasted on every run.
    """
    deadline = time.monotonic() + timeout

    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise VirtualDisplayError(
                f"Xvfb exited immediately with status {process.returncode}. "
                f"Run `Xvfb :{number}` by hand to see what it says."
            )

        if display_in_use(number):
            return

        time.sleep(READY_POLL_SECONDS)

    raise VirtualDisplayError(f"Xvfb did not come up on :{number} within {timeout:g}s.")


@contextmanager
def virtual_display(
    width: int = DEFAULT_WIDTH,
    height: int = DEFAULT_HEIGHT,
    depth: int = DEFAULT_DEPTH,
) -> Iterator[str]:
    """Run the body with DISPLAY pointing at a private Xvfb screen.

    `os.environ` is what is changed, rather than an env dict threaded through
    the driver: the application, `xwindow_capture`, and anything else the run
    shells out to all inherit it, so none of them need to know a virtual display
    exists. The previous value is restored on the way out, including when the
    body raises.
    """
    if not is_available():
        raise VirtualDisplayError(missing_message())

    number = find_free_display()
    process = subprocess.Popen(
        [
            "Xvfb",
            f":{number}",
            "-screen",
            "0",
            f"{width}x{height}x{depth}",
            # No TCP socket: this screen is for one process tree on this
            # machine, and a listening port is an invitation nobody needs.
            "-nolisten",
            "tcp",
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    previous = os.environ.get("DISPLAY")

    try:
        wait_until_ready(number, process)
        os.environ["DISPLAY"] = f":{number}"
        yield f":{number}"
    finally:
        if previous is None:
            os.environ.pop("DISPLAY", None)
        else:
            os.environ["DISPLAY"] = previous

        process.terminate()

        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
