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
    """The X utilities this pass needs, compiled on demand.

    `pointer_control` is deliberately not among them. An X capture of a window
    does not include the cursor, so the only thing parking the pointer changes
    is hover state -- which matters when images are compared pixel for pixel, as
    the golden-image validation does, and does not matter here, where a person
    looks at them. `run-ui-golden.zsh` still parks for exactly that reason.
    """

    capture: Path
    window_control: Path

    @classmethod
    def ensure(cls, build_directory: Path = TOOL_BUILD_DIRECTORY) -> "Tooling":
        build_directory.mkdir(parents=True, exist_ok=True)
        binaries = {
            "xwindow_capture": ("x11",),
            "window_control": ("x11",),
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

        return cls(built["xwindow_capture"], built["window_control"])


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


def duplicate_problem(
    state: str, geometry: str, digest: str, seen: dict[tuple[str, str], str]
) -> str:
    """Whether this capture is byte-identical to a *different* state's, at the same size.

    The backstop for photographing the wrong window. `seen` maps (resolution,
    digest) to the state that produced it; two different states producing
    identical pixels at the same resolution means one of them captured something
    that was not its subject. That is how the settings surface was caught coming
    back byte-identical to the empty shell — nothing named the settings window,
    so the X utilities took the first window the manager listed for the process.

    **Compared only within one resolution**, because across resolutions
    identical images are expected and correct: `narrow-window` *is* the tuner
    docked at the narrow geometry, and the sweep's constrained resolution is
    that same geometry, so those two captures are the same picture by
    definition. Comparing across resolutions reported that as a failed capture.

    Two surfaces sharing a *state* are expected to match, and say nothing.

    The result is a **notice on a kept capture**, not a failure. It began as a
    failure and threw away good images: `settings-appearance` is identical to
    `settings-open` because the appearance panel is the settings window's
    default panel, and `all-tools-docked` matched `two-tools-split` at 800x600.
    Whether that is a bug is a judgement, which is the reviewer's to make with
    the image in front of them — so it is flagged, not deleted.
    """
    other = seen.get((geometry, digest))

    if other is None or other == state:
        return ""

    return (
        f"identical to '{other}' at this resolution — either these surfaces really do "
        f"look the same, or this one photographed that window instead"
    )


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
        sleep: Callable[[float], None] = time.sleep,
    ) -> None:
        self.store = store
        self.run_id = run_id
        self.driver = driver
        self.tooling = tooling
        self.image_directory = image_directory
        self.window_title = window_title
        self.settle_seconds = settle_seconds
        self.poll_interval = poll_interval
        self.sleep = sleep

    # --- X plumbing ---------------------------------------------------------

    def _window_arguments(self, title: str = "") -> list[str]:
        """Which window to act on: the surface's own, or the run's default.

        A surface that owns a top-level window -- settings, feedback, a floating
        tool -- must name it. Without a title the X utilities take whichever
        window the window manager lists first for the process, which is the main
        one, and the capture silently photographs the wrong thing.
        """
        chosen = title or self.window_title or ""

        return ["--title", chosen] if chosen else []

    def read_size(self, title: str = "") -> tuple[int, int] | None:
        pid = self.driver.pid

        if pid is None:
            return None

        completed = subprocess.run(
            [str(self.tooling.window_control), str(pid), "geometry", *self._window_arguments(title)],
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

    def _capture_to(self, destination: Path, title: str = "") -> str:
        pid = self.driver.pid

        if pid is None:
            return "the application is not running"

        completed = subprocess.run(
            [str(self.tooling.capture), str(pid), str(destination), *self._window_arguments(title)],
            capture_output=True,
            text=True,
            check=False,
        )

        if completed.returncode != 0:
            return completed.stderr.strip() or "the capture utility failed"

        return ""

    # --- The pass -----------------------------------------------------------

    def move_to(self, surface: surfaces.Surface, geometry: str, theme: str = "") -> str:
        """Put the application on a surface, at a resolution, in a palette.

        The theme is applied after the state, not before: opening a state
        rebuilds the workspace, and a palette set first would be correct for the
        shell and stale for whatever the state just created.
        """
        try:
            if surface.restart_before:
                self.driver.restart()

            reply = self.driver.open_state(surface.state)

            if not reply.success:
                return reply.error

            if theme:
                reason = self.driver.set_theme(theme)

                if reason:
                    return reason

            if not surface.fixed_geometry:
                return self.driver.set_geometry(geometry)

            return ""
        except ChannelError as error:
            return str(error)

    def capture_one(
        self,
        surface: surfaces.Surface,
        geometry: str,
        seen: dict[str, tuple[int, int]],
        digests: dict[tuple[str, str], str] | None = None,
        theme: str = surfaces.DARK,
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
                theme=theme,
                failure=reason,
            )

        reason = self.move_to(surface, geometry, theme)

        if reason:
            fail(f"could not reach the surface: {reason}")

            return None

        size = settled_size(
            lambda: self.read_size(surface.window_title),
            settle_seconds=self.settle_seconds,
            interval=self.poll_interval,
        )

        # After settling, not instead of it: the window is already the right
        # size, and this is time for the tool to have something to show.
        if surface.warmup_seconds > 0:
            self.sleep(surface.warmup_seconds)
        problem = geometry_problem(geometry, size, seen)

        if problem:
            fail(problem)

            return None

        stem = f"{slug(surface.state)}--{slug(surface.title)}--{slug(geometry)}--{slug(theme)}"
        ppm_path = self.image_directory / f"{stem}.ppm"
        png_path = self.image_directory / f"{stem}.png"
        thumbnail_path = self.image_directory / f"{stem}.thumb.png"
        self.image_directory.mkdir(parents=True, exist_ok=True)

        reason = self._capture_to(ppm_path, surface.window_title)

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

        # Keyed by palette as well: the same surface in dark and in light is
        # expected to differ, and two surfaces matching in one palette says
        # nothing about the other.
        notice = duplicate_problem(
            surface.state, f"{geometry}/{theme}", digest, digests if digests is not None else {}
        )

        if digests is not None:
            digests.setdefault((f"{geometry}/{theme}", digest), surface.state)

        self.store.record_capture(
            self.run_id,
            state=surface.state,
            title=surface.title,
            geometry=geometry,
            theme=theme,
            image_path=str(png_path),
            thumbnail_path=str(thumbnail_path),
            width=width,
            height=height,
            digest=digest,
            notice=notice,
        )

        return size

    def run(
        self,
        plan: tuple[tuple[surfaces.Surface, str, str], ...],
        *,
        resume: bool = True,
        progress: Callable[[dict], None] | None = None,
        should_stop: Callable[[], bool] | None = None,
    ) -> dict:
        """Walk the plan. Never raises for one surface's sake.

        `progress` is called after each surface, so a caller running this in the
        background -- the review page's capture button -- can say what is
        happening rather than showing a spinner for ten minutes.

        `should_stop` is asked at each surface boundary. Between surfaces is the
        only safe place: one capture asks for a geometry, waits for the window
        to settle, photographs it, converts it, and writes a row, and stopping
        part way through leaves a partial image or a row pointing at no file. A
        surface takes seconds, so the delay between asking to stop and stopping
        is bounded by one of them.

        Surfaces not reached are counted as stopped, never as failed. A stopped
        run and a broken one are different, and the export feeds a release
        gate.
        """
        already = self.store.captured_keys(self.run_id) if resume else set()
        sizes: dict[str, dict[str, tuple[int, int]]] = {}

        # (resolution, digest) -> the state that produced it: two states with the
        # same pixels at the same resolution means one captured the wrong window.
        digests: dict[tuple[str, str], str] = {}
        captured = 0
        failed = 0
        skipped = 0
        stopped = False

        for surface, geometry, theme in plan:
            if should_stop is not None and should_stop():
                stopped = True

                break

            if (surface.state, surface.title, geometry, theme) in already:
                skipped += 1

                continue

            if progress is not None:
                progress(
                    {
                        "surface": surface.title,
                        "geometry": geometry,
                        "theme": theme,
                        "done": captured + failed + skipped,
                        "total": len(plan),
                    }
                )

            # Sizes are compared within one palette: a theme change does not
            # resize anything, but keying them apart keeps the check honest if
            # that ever stops being true.
            seen = sizes.setdefault(f"{surface.title}/{theme}", {})
            size = self.capture_one(surface, geometry, seen, digests, theme)

            if size is None:
                failed += 1

                continue

            captured += 1
            seen[geometry] = size

        return {
            "captured": captured,
            "failed": failed,
            "already_captured": skipped,
            "stopped": stopped,
            # What the run did not reach. Reported separately from failures so
            # nothing downstream mistakes "we stopped" for "the build is broken".
            "not_reached": max(0, len(plan) - captured - failed - skipped) if stopped else 0,
        }
