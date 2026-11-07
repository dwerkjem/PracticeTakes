#!/usr/bin/env python3
"""Every application a run started, and nobody else's.

A parallel run starts several applications, and some of them go wrong: one comes
up without an input device and never answers, another has to be replaced. Ending
those is necessary. Ending them *by name* is not the same thing, and the
difference matters on the machine this runs on, where the developer very
reasonably has Practice Takes open while the suite works.

So the run keeps the process ids it started. Teardown ends those and nothing
else, and a report about what is running says which ones were the run's.

The other half of the same fact: an application the run did not start may hold
the input device. Workers open none (#176), so only a surface that asks for a
real device contends with it, and that is worth saying out loud at the start of
a run rather than discovering as a capture that never replied -- so this can
also name the instances such a surface is competing with, without touching
them. #180 measured everything else capturing normally beside one.

Linux only, by reading `/proc`. The capture pass is Linux-only anyway: it drives
X utilities compiled against Xlib.

Standard library only.
"""

from __future__ import annotations

from pathlib import Path
import threading

PROCESS_ROOT = Path("/proc")

# What the application is called in the process table.
APPLICATION = "PracticeTakes"


def running_instances(root: Path = PROCESS_ROOT) -> set[int]:
    """Every process id on this machine that is the application.

    `comm` rather than the command line, because the command line of anything
    that *mentions* the application -- a build, an editor, this suite -- would
    match too, and a check that catches its own tooling is worse than no check.
    """
    found: set[int] = set()

    for entry in root.glob("[0-9]*"):
        try:
            if (entry / "comm").read_text().strip() == APPLICATION:
                found.add(int(entry.name))
        except (OSError, ValueError):
            # Gone between the listing and the read, which is normal.
            continue

    return found


class Fleet:
    """The applications one run started.

    A worker registers its application as soon as it has one and removes it when
    it has finished with it, so `pids` is what this run currently owns. Nothing
    here ends a process that was not registered.
    """

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._drivers: dict[int, object] = {}
        # Everything ever registered, kept after removal so a report at the end
        # can still tell the run's processes from somebody else's.
        self._seen: set[int] = set()

    def add(self, driver) -> None:
        pid = getattr(driver, "pid", None)

        if pid is None:
            return

        with self._lock:
            self._drivers[int(pid)] = driver
            self._seen.add(int(pid))

    def remove(self, driver) -> None:
        pid = getattr(driver, "pid", None)

        with self._lock:
            for known, held in list(self._drivers.items()):
                if held is driver or (pid is not None and known == int(pid)):
                    del self._drivers[known]

    def pids(self) -> set[int]:
        with self._lock:
            return set(self._drivers)

    def started(self) -> set[int]:
        with self._lock:
            return set(self._seen)

    def foreign(self, root: Path = PROCESS_ROOT) -> set[int]:
        """Instances running that this run did not start.

        These are not a problem to solve here -- somebody is using their
        computer -- but they hold the input device, and a worker that cannot
        open it comes up and never answers. Naming them turns a mystifying
        failure into a sentence.
        """
        return running_instances(root) - self.started()

    def shut_down(self) -> int:
        """End what this run started, and only that. Returns how many."""
        with self._lock:
            drivers = list(self._drivers.values())
            self._drivers.clear()

        for driver in drivers:
            try:
                driver.kill()
            except Exception:  # noqa: BLE001 - teardown reports nothing to nobody
                continue

        return len(drivers)


def contention_warning(foreign: set[int], surfaces_needing_device: tuple[str, ...] = ()) -> str:
    """What to say about instances the run is competing with, or "".

    Only the surfaces that open a real input device can contend with an
    application somebody else is running. Every other worker opens none
    (`--no-audio-input`, #176), and #180 measured them capturing normally beside
    an open application. So the warning is about those surfaces, by name, and
    says nothing at all when the plan has none.
    """
    if not foreign or not surfaces_needing_device:
        return ""

    instances = "instance is" if len(foreign) == 1 else "instances are"
    holder = "It" if len(foreign) == 1 else "They"
    names = ", ".join(surfaces_needing_device)

    return (
        f"{len(foreign)} other Practice Takes {instances} already running "
        f"(pid {', '.join(str(pid) for pid in sorted(foreign))}). {holder} may hold "
        f"the input device, which {names} opens, so that capture may wait for it or "
        f"come up without one. Every other surface opens no device and is "
        f"unaffected. Nothing here will close them."
    )
