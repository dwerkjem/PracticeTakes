#!/usr/bin/env python3
"""Opening the real application on the surface you are looking at.

A screenshot answers "does this look right" and stops there. The moment it
raises a question — is that control actually disabled, does this menu open, is
that really the whole panel — the only answer is the application itself, and
finding it by hand means remembering which state, which palette, and which
window size produced the image.

So every capture in the grid carries a link that puts a live application into
exactly that state. It is the same control channel the capture pass uses, doing
the same thing it did to take the picture; the difference is that nobody quits
it afterwards.

One at a time, deliberately. Two instances would fight over the audio device and
over which window a later capture belongs to, and a reviewer who has lost track
of how many are open has lost track of what they are looking at.

Standard library only.
"""

from __future__ import annotations

import datetime as _datetime
from pathlib import Path
import threading

import surfaces
from driver import ApplicationDriver, ChannelError


class LaunchError(RuntimeError):
    """The application could not be opened on that surface."""


class ManualLaunch:
    """The one application instance a reviewer has open for a closer look."""

    def __init__(self, executable: Path) -> None:
        self.executable = executable
        self._driver: ApplicationDriver | None = None

        # Reentrant, because open() and close() both report their result by
        # calling status(), which takes the lock again. With a plain Lock that
        # is a deadlock -- a hub that hangs on the first click, and a test suite
        # that hangs before printing anything.
        self._lock = threading.RLock()
        self._what: dict = {}

    # --- Status -------------------------------------------------------------

    @property
    def running(self) -> bool:
        driver = self._driver

        return driver is not None and driver.pid is not None

    def status(self) -> dict:
        with self._lock:
            return {
                "running": self.running,
                "pid": self._driver.pid if self._driver else None,
                **self._what,
            }

    # --- Opening and closing ------------------------------------------------

    def open(self, *, state: str, geometry: str = "", theme: str = "") -> dict:
        """Put a live application on a surface, replacing whatever was open."""
        if state not in surfaces.required_states():
            raise LaunchError(f"'{state}' is not a surface this suite knows about")

        if not self.executable.is_file():
            raise LaunchError(
                f"no application at {self.executable} — build it with "
                f"-DPRACTICE_TAKES_ENABLE_TEST_CONTROL=ON first"
            )

        with self._lock:
            self._close_locked()

            driver = ApplicationDriver(self.executable)

            try:
                driver.start()
                reply = driver.open_state(state)

                if not reply.success:
                    raise LaunchError(reply.error)

                # Palette then size, in the order the capture pass used, so what
                # opens is what was photographed rather than a near miss.
                if theme:
                    reason = driver.set_theme(theme)

                    if reason:
                        raise LaunchError(reason)

                if geometry and geometry != surfaces.DEFAULT_GEOMETRY:
                    reason = driver.set_geometry(geometry)

                    if reason:
                        raise LaunchError(reason)
            except (ChannelError, LaunchError) as error:
                driver.stop()

                raise LaunchError(str(error)) from error

            self._driver = driver
            self._what = {
                "state": state,
                "geometry": geometry or surfaces.DEFAULT_GEOMETRY,
                "theme": theme or surfaces.DARK,
                "opened_at": _datetime.datetime.now().replace(microsecond=0).isoformat(),
            }

            return self.status()

    def close(self) -> dict:
        with self._lock:
            self._close_locked()

            return self.status()

    def _close_locked(self) -> None:
        if self._driver is not None:
            try:
                self._driver.stop()
            except ChannelError:
                pass

            self._driver = None
            self._what = {}
