#!/usr/bin/env python3
"""The unattended pass: drive the application to every surface and photograph it.

Nobody is present while this runs. That is the whole point — it is what lets the
review be one pass over a contact sheet instead of a serial interrogation with a
tester sitting through it.

Two things are worth understanding before changing anything here.

**Nothing is synthesised.** The application is asked for an approved state and
for a named geometry over its own control channel; the suite never clicks at
coordinates and never resizes the window from outside. An external resize cannot
reach the sizes that matter anyway — the window advertises a minimum width above
the threshold at which the title bar collapses — so a layout problem that only
appears in a narrow window would be invisible to it.

**A geometry has to be confirmed, not assumed.** Asking for "constrained" and
capturing immediately is how you get three identical images labelled as three
different sizes, which is exactly the failure this workflow exists to catch. So
the window is polled until its size settles, and a resolution that produced the
same size as the default is recorded as a capture failure rather than as a
picture of nothing in particular.

Standard library only, except for the image conversion in `images.py`.
"""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path
import re
import subprocess
import time

import images
import surfaces
from driver import ApplicationDriver, ChannelError
from store import Store

REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
UI_VALIDATION_SOURCE = REPOSITORY_ROOT / "tools" / "scripts" / "quality" / "ui-validation"
TOOL_BUILD_DIRECTORY = REPOSITORY_ROOT / "build" / "ui-validation"

# How long a window is given to adopt a geometry before the attempt is recorded
# as a failure. Generous: a window manager under load is slow, and a false
# failure here costs a recapture while a false success costs a wrong verdict.
SETTLE_SECONDS = 6.0
POLL_INTERVAL_SECONDS = 0.25
STABLE_POLLS = 3


class CaptureError(RuntimeError):
    """The pass cannot run at all — as distinct from one surface failing."""


@dataclass(frozen=True)
class Tooling:
    """The three X utilities, compiled on demand."""

    capture: Path
    window_control: Path
    pointer: Path

    @classmethod
    def ensure(cls, build_directory: Path = TOOL_BUILD_DIRECTORY) -> "Tooling":
        build_directory.mkdir(parents=True, exist_ok=True)
        binaries = {
            "xwindow_capture": ("x11",),
            "window_control": ("x11",),
            "pointer_control": ("x11", "xtst"),
        }
        built: dict[str, Path] = {}

        for name, libraries in binaries.items():
            source = UI_VALIDATION_SOURCE / f"{name}.cpp"
            binary = build_directory / name

            if not binary.exists() or binary.stat().st_mtime < source.stat().st_mtime:
                flags = _pkg_config(libraries)
                completed = subprocess.run(
                    ["/usr/bin/g++", "-std=c++20", "-O2", str(source), "-o", str(binary), *flags],
                    capture_output=True,
                    text=True,
                    check=False,
                )

                if completed.returncode != 0:
                    raise CaptureError(f"could not build {name}:\n{completed.stderr.strip()}")

            built[name] = binary

        return cls(built["xwindow_capture"], built["window_control"], built["pointer_control"])


def _pkg_config(libraries: tuple[str, ...]) -> list[str]:
    completed = subprocess.run(
        ["pkg-config", "--cflags", "--libs", *libraries],
        capture_output=True,
        text=True,
        check=False,
    )

    if completed.returncode != 0:
        raise CaptureError(f"pkg-config could not describe {' '.join(libraries)}")

    return completed.stdout.split()


def slug(text: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")


def settled_size(
    read_size: Callable[[], tuple[int, int] | None],
    *,
    settle_seconds: float = SETTLE_SECONDS,
    interval: float = POLL_INTERVAL_SECONDS,
    stable_polls: int = STABLE_POLLS,
    sleep: Callable[[float], None] = time.sleep,
    clock: Callable[[], float] = time.monotonic,
) -> tuple[int, int] | None:
    """Poll until the window size stops changing, or give up.

    Pure apart from the two injected callables, so the settling rule is tested
    without a window manager.
    """
    deadline = clock() + settle_seconds
    previous: tuple[int, int] | None = None
    repeats = 0

    while clock() < deadline:
        size = read_size()

        if size is not None and size == previous:
            repeats += 1

            if repeats >= stable_polls:
                return size
        else:
            repeats = 1 if size is not None else 0
            previous = size

        sleep(interval)

    return None


def geometry_problem(
    geometry: str, size: tuple[int, int] | None, seen: dict[str, tuple[int, int]]
) -> str:
    """Why this capture must not be trusted, or "" when it can be.

    `seen` is what this surface already captured at, keyed by resolution. The
    interesting case is the second one: a window that ignored the request and
    kept the size it already had still captures perfectly well — it just
    captures the same thing again, under a label claiming otherwise. That is not
    hypothetical; it is the bug that made a sweep report a narrow window as "not
    narrow" and a fullscreen window as "not fullscreen".
    """
    if size is None:
        return "the window never settled at a size, so it was not captured"

    for other, other_size in seen.items():
        if other != geometry and other_size == size:
            return (
                f"the window stayed at {size[0]}x{size[1]} — the size it was captured at "
                f"for '{other}' — when '{geometry}' was requested"
            )

    return ""


class CapturePass:
    """One unattended walk through the plan."""

    def __init__(
        self,
        *,
        store: Store,
        run_id: int,
        driver: ApplicationDriver,
        tooling: Tooling,
        image_directory: Path,
        window_title: str | None = None,
        settle_seconds: float = SETTLE_SECONDS,
        poll_interval: float = POLL_INTERVAL_SECONDS,
    ) -> None:
        self.store = store
        self.run_id = run_id
        self.driver = driver
        self.tooling = tooling
        self.image_directory = image_directory
        self.window_title = window_title
        self.settle_seconds = settle_seconds
        self.poll_interval = poll_interval

    # --- X plumbing ---------------------------------------------------------

    def _window_arguments(self) -> list[str]:
        return ["--title", self.window_title] if self.window_title else []

    def read_size(self) -> tuple[int, int] | None:
        pid = self.driver.pid

        if pid is None:
            return None

        completed = subprocess.run(
            [str(self.tooling.window_control), str(pid), "geometry", *self._window_arguments()],
            capture_output=True,
            text=True,
            check=False,
        )

        if completed.returncode != 0:
            return None

        parts = completed.stdout.split()

        if len(parts) != 2:
            return None

        try:
            return int(parts[0]), int(parts[1])
        except ValueError:
            return None

    def park_pointer(self) -> None:
        """Move the pointer out of the way so no capture shows a hover state."""
        subprocess.run([str(self.tooling.pointer), "--park"], capture_output=True, check=False)

    def _capture_to(self, destination: Path) -> str:
        pid = self.driver.pid

        if pid is None:
            return "the application is not running"

        completed = subprocess.run(
            [str(self.tooling.capture), str(pid), str(destination), *self._window_arguments()],
            capture_output=True,
            text=True,
            check=False,
        )

        if completed.returncode != 0:
            return completed.stderr.strip() or "the capture utility failed"

        return ""

    # --- The pass -----------------------------------------------------------

    def move_to(self, surface: surfaces.Surface, geometry: str) -> str:
        """Put the application on a surface at a resolution. Returns a reason on failure."""
        try:
            if surface.restart_before:
                self.driver.restart()

            reply = self.driver.open_state(surface.state)

            if not reply.success:
                return reply.error

            if not surface.fixed_geometry:
                return self.driver.set_geometry(geometry)

            return ""
        except ChannelError as error:
            return str(error)

    def capture_one(
        self, surface: surfaces.Surface, geometry: str, seen: dict[str, tuple[int, int]]
    ) -> tuple[int, int] | None:
        """Capture one surface at one resolution, recording whatever happened.

        Returns the window size when an image was stored, so the caller can hold
        each surface's sizes as the yardstick for its remaining resolutions.
        """
        def fail(reason: str) -> None:
            self.store.record_capture(
                self.run_id,
                state=surface.state,
                title=surface.title,
                geometry=geometry,
                failure=reason,
            )

        reason = self.move_to(surface, geometry)

        if reason:
            fail(f"could not reach the surface: {reason}")

            return None

        self.park_pointer()
        size = settled_size(
            self.read_size, settle_seconds=self.settle_seconds, interval=self.poll_interval
        )
        problem = geometry_problem(geometry, size, seen)

        if problem:
            fail(problem)

            return None

        stem = f"{slug(surface.state)}--{slug(surface.title)}--{slug(geometry)}"
        ppm_path = self.image_directory / f"{stem}.ppm"
        png_path = self.image_directory / f"{stem}.png"
        thumbnail_path = self.image_directory / f"{stem}.thumb.png"
        self.image_directory.mkdir(parents=True, exist_ok=True)

        reason = self._capture_to(ppm_path)

        if reason:
            fail(f"the window could not be captured: {reason}")

            return None

        try:
            width, height, digest = images.convert(ppm_path, png_path, thumbnail_path)
        except images.ImageError as error:
            fail(str(error))

            return None
        finally:
            ppm_path.unlink(missing_ok=True)

        self.store.record_capture(
            self.run_id,
            state=surface.state,
            title=surface.title,
            geometry=geometry,
            image_path=str(png_path),
            thumbnail_path=str(thumbnail_path),
            width=width,
            height=height,
            digest=digest,
        )

        return size

    def run(self, plan: tuple[tuple[surfaces.Surface, str], ...], *, resume: bool = True) -> dict:
        """Walk the plan. Never raises for one surface's sake."""
        already = self.store.captured_keys(self.run_id) if resume else set()
        sizes: dict[str, dict[str, tuple[int, int]]] = {}
        captured = 0
        failed = 0
        skipped = 0

        for surface, geometry in plan:
            if (surface.state, surface.title, geometry) in already:
                skipped += 1

                continue

            seen = sizes.setdefault(surface.title, {})
            size = self.capture_one(surface, geometry, seen)

            if size is None:
                failed += 1

                continue

            captured += 1
            seen[geometry] = size

        return {"captured": captured, "failed": failed, "already_captured": skipped}
